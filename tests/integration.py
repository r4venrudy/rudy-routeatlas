"""Independent linear origin-validation oracle for the native CLI."""
from collections import Counter
import ipaddress as ip
import json
from pathlib import Path
import random
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
examples = Path(sys.argv[2]).resolve()
checks = 0
states = ("Valid", "NotFound", "Invalid")


def check(condition, context):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(context)


def call(*args, expected=0):
    result = subprocess.run([binary, *args], capture_output=True, text=True, timeout=30)
    check(result.returncode == expected, (args, result.returncode, result.stderr))
    if expected == 2:
        check(not result.stdout and "routeatlas:" in result.stderr, result)
        return None
    check(not result.stderr, result.stderr)
    return json.loads(result.stdout)


def verdict(rows, route, origin):
    covered = [(net, m, a) for net, m, a in rows
               if route.version == net.version and route.subnet_of(net)]
    same = [r for r in covered if r[2] == origin and origin]
    matching = [r for r in same if route.prefixlen <= r[1]]
    as0 = sum(r[2] == 0 for r in covered)
    if matching:
        state, reason = "Valid", "authorized"
        witness = min(matching, key=lambda x: (-x[0].prefixlen, x[1]))
    elif same:
        state, reason = "Invalid", "max-length"
        witness = max(same, key=lambda x: (x[0].prefixlen, x[1]))
    elif covered:
        state = "Invalid"
        reason = "unknown-origin" if not origin else "as0-only" if as0 == len(covered) else "origin-mismatch"
        witness = min(covered, key=lambda x: (-x[0].prefixlen, x[2], x[1]))
    else:
        state, reason, witness = "NotFound", "uncovered", None
    return dict(state=state, reason=reason, covered=len(covered), matched=len(matching),
                as0=as0, witness=None if witness is None else
                dict(prefix=str(witness[0]), maxLength=witness[1], asn=witness[2]))


def read_vrps(path):
    return {(ip.ip_network(p), int(m), int(a))
            for p, m, a in (line.split(",") for line in path.read_text().splitlines()[1:])}


def read_routes(path):
    return {(ip.ip_network(p), 0 if a == "NONE" else int(a))
            for p, a in (line.split(",") for line in path.read_text().splitlines()[1:])}


def check_compare(before, after, routes, result):
    matrix = [[0] * 3 for _ in states]
    found = set()
    for item in result["routes"]:
        prefix, origin = ip.ip_network(item["prefix"]), item["origin"] or 0
        check((prefix, origin) in routes and (prefix, origin) not in found, item)
        found.add((prefix, origin))
        old, new = verdict(before, prefix, origin), verdict(after, prefix, origin)
        check(item["before"] == old and item["after"] == new, (item, old, new))
        i, j = states.index(old["state"]), states.index(new["state"])
        matrix[i][j] += 1
        check(item["regressed"] == (j > i), item)
    check(found == routes, (len(found), len(routes)))
    check(result["matrix"] == matrix and result["stateOrder"] == list(states), result["matrix"])
    check(result["regressions"] == sum(matrix[i][j] for i in range(3) for j in range(i+1, 3)), result)
    check(result["beforeVrps"] == len(before) and result["afterVrps"] == len(after), result)


fixed = [examples / name for name in ("before.csv", "after.csv", "routes.csv")]
result = call("compare", *fixed, "--fail-on-regression", expected=1)
check_compare(read_vrps(fixed[0]), read_vrps(fixed[1]), read_routes(fixed[2]), result)
check(result["regressions"] == 1, result)
check(call("compare", *fixed) == result, "exit policy changes evidence")
with tempfile.TemporaryDirectory() as folder:
    before_path, after_path, routes_path = [Path(folder) / (name + ".csv") for name in ("before", "after", "routes")]
    rng = random.Random(68118481)
    blocks = [ip.ip_network("10.0.0.0/8"), ip.ip_network("2001:db8::/32")]
    asns = [0, 64496, 64497, 64498, 65551, 4200000000, 4294967295]
    before, after, routes = set(), set(), set()
    for block in blocks:
        width = block.max_prefixlen
        for _ in range(750):
            length = rng.randrange(block.prefixlen, width + 1)
            address = int(block.network_address) | rng.getrandbits(width - block.prefixlen)
            # Use the source address family even when the numeric address is small.
            prefix = type(block)((address, length), strict=False)
            vrp = (prefix, rng.randrange(length, width + 1), rng.choice(asns))
            if rng.random() < 0.8:
                before.add(vrp)
            if rng.random() < 0.8:
                after.add(vrp)
            route_length = rng.randrange(length, width + 1)
            routes.add((type(block)((address, route_length), strict=False), rng.choice(asns)))
    routes.update({(ip.ip_network("::/0"), 64496), (ip.ip_network("0.0.0.0/0"), 64497)})
    before.add((ip.ip_network("::/0"), 128, 0))
    after.add((ip.ip_network("0.0.0.0/0"), 32, 64496))
    for path, rows in ((before_path, before), (after_path, after)):
        lines = [f"{prefix},{length},{asn}" for prefix, length, asn in rows]
        rng.shuffle(lines)
        path.write_bytes(("prefix,maxLength,asn\r\n" + "\r\n".join(lines + lines[:10])).encode())
    lines = [f"{prefix},{asn or 'NONE'}" for prefix, asn in routes]
    rng.shuffle(lines)
    routes_path.write_text("prefix,origin\n" + "\n".join(lines + lines[:10]), encoding="ascii")
    report = call("compare", before_path, after_path, routes_path)
    check_compare(before, after, routes, report)
    inspected = call("validate", before_path, routes_path)
    counts = Counter()
    for item in inspected["routes"]:
        got = item["validation"]
        want = verdict(before, ip.ip_network(item["prefix"]), item["origin"] or 0)
        check(got == want, (got, want))
        counts[got["state"]] += 1
    check(inspected["counts"] == {state: counts[state] for state in states}, inspected["counts"])
    check(call("compare", before_path, before_path, routes_path, "--fail-on-regression")["regressions"] == 0, "identity gate")
    routes_path.write_text("prefix,origin\n::ffff:192.0.2.1/128,4294967295\n::ffff:c000:201/128,4294967295")
    canonical = call("validate", before_path, routes_path)["routes"]
    check(len(canonical) == 1 and canonical[0]["prefix"] == "::ffff:c000:201/128", canonical)
    for invalid in (
        b"wrong\n192.0.2.0/24,1", b"prefix,origin\n192.0.2.1/24,1",
        b"prefix,origin\n192.0.02.0/24,1", b"prefix,origin\n:::/0,1",
        b"prefix,origin\n2001:db8::/129,1", b"prefix,origin\n::%if0/0,1",
        b"prefix,origin\n0.0.0.0/0,0", b"prefix,origin\n::/0,4294967296",
        b"prefix,origin\n::/0,+1", b"prefix,origin\n::/0,1,2",
        b"prefix,origin\n::/0,1\x00", b"prefix,origin\n" + b"a"*513,
    ):
        routes_path.write_bytes(invalid)
        call("validate", before_path, routes_path, expected=2)
    routes_path.write_text("prefix,origin\n192.0.2.0/24,64496\n")
    for vrp in ("192.0.2.0/24,23,1", "::/0,129,1", "0.0.0.0/0,32,-1", "::/0,128,4294967296"):
        before_path.write_text("prefix,maxLength,asn\n" + vrp)
        call("validate", before_path, routes_path, expected=2)
    before_path.write_text("prefix,maxLength,asn")
    check(call("validate", before_path, routes_path)["counts"]["NotFound"] == 1, "empty VRP export")
print(f"{checks} independent checks across the dual-stack fixture and 1,500 generated routes")
