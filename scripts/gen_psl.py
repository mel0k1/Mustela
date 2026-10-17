#!/usr/bin/env python3
import argparse
import io
import sys
import urllib.request

SOURCE_URL = "https://publicsuffix.org/list/public_suffix_list.dat"
OUTPUT_DEFAULT = "src/core/psl_data.inc"


def to_ascii_rule(rule):
    labels = rule.split(".")
    out = []
    for label in labels:
        if label.isascii():
            out.append(label.lower())
        else:
            out.append("xn--" + label.lower().encode("punycode").decode("ascii"))
    return ".".join(out)


def parse(dat_text):
    version = ""
    exact = set()
    wildcard = set()
    exception = set()
    for raw in dat_text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("//"):
            body = line[2:].strip()
            if body.startswith("VERSION: "):
                version = body[len("VERSION: "):].strip()
            continue
        if line.startswith("*."):
            wildcard.add(to_ascii_rule(line[2:]))
            continue
        if line.startswith("!"):
            exception.add(to_ascii_rule(line[1:]))
            continue
        exact.add(to_ascii_rule(line))
    return version, sorted(exact), sorted(wildcard), sorted(exception)


def emit_array(name, values, out):
    out.write("const char* const %s[] = {\n" % name)
    for i in range(0, len(values), 8):
        chunk = values[i:i + 8]
        out.write("    " + " ".join('"%s",' % v for v in chunk) + "\n")
    out.write("};\n")
    out.write("const size_t %sCount = sizeof(%s) / sizeof(%s[0]);\n\n" % (name, name, name))


def emit(version, exact, wildcard, exception, out):
    out.write("const char* const kPslVersion = \"%s\";\n\n" % version)
    emit_array("kExactRules", exact, out)
    emit_array("kWildcardRules", wildcard, out)
    emit_array("kExceptionRules", exception, out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", help="use a local public_suffix_list.dat instead of downloading")
    parser.add_argument("--output", default=OUTPUT_DEFAULT)
    args = parser.parse_args()
    if args.input:
        with io.open(args.input, "r", encoding="utf-8") as handle:
            text = handle.read()
    else:
        with urllib.request.urlopen(SOURCE_URL, timeout=60) as response:
            text = response.read().decode("utf-8")
    version, exact, wildcard, exception = parse(text)
    with io.open(args.output, "w", encoding="utf-8", newline="\n") as handle:
        emit(version, exact, wildcard, exception, handle)
    print("psl version %s: %d exact, %d wildcard, %d exception" % (
        version, len(exact), len(wildcard), len(exception)))


if __name__ == "__main__":
    sys.exit(main())
