#!/usr/bin/env python3
"""Report asset references whose capitalisation differs from the file on disk.

The shipped Luxor 3 data was authored on Windows and mixes capitalisation
freely (e.g. a config says "data/gfx/UI/buttons/MM_top_arrow_Red" while the
file is "data/gfx/UI/buttons/MM_top_arrow_red.png"). Windows tolerates that;
a case-sensitive Linux filesystem does not, so the port resolves every path
case-insensitively.

This tool lists the affected references so the behaviour can be verified and
so a future asset repack can be checked for new mismatches.

Usage: python3 case_audit.py <game_dir>
"""
import os, re, sys, collections

EXTS = ['.png', '.jpg', '.jpeg', '.ogg', '.effect', '.txt', '.fontdef', '.LX3TRK']


def main(root):
    exact = set()
    lowered = {}
    for dp, dn, fn in os.walk(root):
        for f in fn:
            rel = os.path.relpath(os.path.join(dp, f), root).replace('\\', '/')
            exact.add(rel)
            lowered.setdefault(rel.lower(), []).append(rel)

    # Two files differing only by case would make case-insensitive lookup
    # ambiguous; the port cannot work around that, so report it loudly.
    collisions = {k: v for k, v in lowered.items() if len(v) > 1}
    print(f"files: {len(exact)}")
    print(f"case-insensitive collisions: {len(collisions)}")
    for k, v in list(collisions.items())[:20]:
        print(f"   COLLISION {k}: {v}")

    def resolve(ref, base=None):
        r = ref.replace('\\', '/').lstrip('/')
        if base:
            r = base.rstrip('/') + '/' + r
        if r in exact:
            return 'exact'
        if r.lower() in lowered:
            return 'case'
        for e in EXTS:
            if (r + e) in exact:
                return 'exact'
            if (r + e).lower() in lowered:
                return 'case'
        return 'missing'

    stats = collections.Counter()
    examples = collections.defaultdict(list)

    def family_match(ref):
        """Animation refs are frame prefixes without an extension.

        For example, ``walk_`` means walk_0001.png and ``ball`` means
        ball0007.png. Match the directory case-insensitively, then match the
        reference against the filename stem (excluding its extension).
        """
        r = ref.replace('\\', '/').lstrip('/').lower()
        directory, _, prefix = r.rpartition('/')
        if not directory:
            return False
        for relative_path in lowered:
            actual_dir, _, filename = relative_path.rpartition('/')
            if actual_dir.lower() != directory:
                continue
            stem = filename.rsplit('.', 1)[0]
            if stem.startswith(prefix):
                return True
        return False

    def resolve_placeable_effect(ref):
        """Resolve Placeables effect name to data/effects/<name>.effect."""
        st = resolve('data/effects/' + ref + '.effect')
        if st == 'missing':
            st = resolve('data/effects/' + ref)
        return st

    def audit(pattern, label, base=None, exts=('.txt',), kind='file'):
        for dp, dn, fn in os.walk(os.path.join(root, 'data')):
            for f in fn:
                if not f.endswith(exts):
                    continue
                p = os.path.join(dp, f)
                t = re.sub(r'//.*', '', open(p, encoding='latin1').read())
                for m in re.finditer(pattern, t):
                    ref = m.group(1)
                    st = resolve(ref, base)
                    if st == 'missing':
                        if kind == 'family':
                            st = 'family' if family_match(ref) else 'missing'
                        elif kind == 'effect':
                            # Placeables name the effect without its extension.
                            st = resolve_placeable_effect(ref)
                    stats[(label, st)] += 1
                    if st == 'case' and len(examples[label]) < 6:
                        examples[label].append((ref, resolve_real(ref, base)))

    def resolve_real(ref, base):
        r = ref.replace('\\', '/').lstrip('/')
        if base:
            r = base.rstrip('/') + '/' + r
        if r in exact:
            return r
        if r.lower() in lowered:
            return lowered[r.lower()][0]
        for e in EXTS:
            if (r + e) in exact:
                return r + e
            if (r + e).lower() in lowered:
                return lowered[(r + e).lower()][0]
        return '(unresolved)'

    audit(r'\bfile(?:Name|NamePlayBackwards)\s+"([^"]+)"', 'animation', kind='family')
    audit(r'\bTexture\s+"([^"]+)"', 'effect', exts=('.effect',))
    audit(r'\b(?:texture|shadowTexture|Icon)\s+"([^"]+)"', 'ui')
    audit(r'\bfile\s+"([^"]+)"', 'sound', base='data/sound')
    # Restrict to Placeables.txt only; .effect files use the same word for
    # nested component semantics and must not be counted as map references.
    audit(r'\beffect\s+"([^"]+)"', 'placeable', kind='effect', exts=('Placeables.txt',))

    print("\nresolution status:")
    for (label, st), n in sorted(stats.items()):
        print(f"  {label:10s} {st:8s} {n}")

    total_case = sum(n for (l, st), n in stats.items() if st == 'case')
    total_missing = sum(n for (l, st), n in stats.items() if st == 'missing')
    total_family = sum(n for (l, st), n in stats.items() if st == 'family')
    print(f"\nreferences needing case-insensitive lookup: {total_case}")
    print(f"references that do not resolve at all:      {total_missing}")
    print(f"animation frame-family references:            {total_family}")

    print("\nexamples:")
    for label, ex in examples.items():
        print(f"  -- {label}")
        for ref, real in ex:
            print(f"     ref : {ref}")
            print(f"     disk: {real}")
    return 0 if total_missing == 0 and not collisions else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
