#!/usr/bin/env python
import sys
import os
if sys.version_info[0] < 3:
    raise ImportError("Cannot import %s. Python 3+ required. "
                      "[Make your life easier, take the plunge!]" %
                      os.path.basename(__file__))


def nk2sketchbytes(sketchtype, n=0, k=0):
    if not (k and n):
        raise Exception("Set n, k kwargs for nk2sketchbytes pls")
    try:
        return {"mash": 8 if k > 16 else 4,
                "hlash": 1}[sketchtype.lower()] << n
    except KeyError:
        sys.stderr.write(
            ("Key error in nk2sketchbytes."
             "key: %s. Expected mash or hlash" % sketchtype))
        raise


def get_flag(tok):
    '''Also works as a boolean, as these flags are guaranteed to be nonzero
    '''
    if not tok:
        return False
    flagchar, rest = tok[0], tok[1:]
    if rest:
        try:
            return int(rest)
        except ValueError:
            # sys.stderr.write("Could not parse flag '%s'\n" % tok)
            pass
    return False


def canonname(k1, k2=None):
    if not k2:
        assert len(k1) == 2
        tmp1, k2 = k1
        k1 = tmp1
    return sorted(map(os.path.basename, (k1, k2)))


def get_nk(path):
    k = n = 0
    for tok, val in ((tok, get_flag(tok)) for tok in
                     os.path.basename(path).split(".")):
        if not val:
            continue
        if tok[0] == 'k':
            k = val
        elif tok[0] == 'n':
            n = val
        elif "exper" not in tok:
            raise ValueError("Unexpected token %s" % tok)
    if not (k and n):
        raise Exception("Invalid mashexp filename %s" % path)
    return n, k


def canonkey(k1, k2=None, n=0, k=0):
    if isinstance(k1, HlashData):
        return k1.key
    if not (k and n):
        raise Exception("ZOMG")
    return tuple((*canonname(k1, k2), n, k))


def mash2dict(path):
    basename = os.path.basename
    with open(path) as ifp:
        try:
            k1 = basename(next(ifp).strip().split()[1])
        except StopIteration:
            print("StopIteration from file with path %s" % path)
            raise
        n, k = get_nk(path)
        ret = {canonkey(k1, i.split()[0], nk2sketchbytes("mash", n, k), k):
               float(i.strip().split()[1]) for i in ifp}
    print("Ret: %s" % ret, file=sys.stderr)
    return ret


def folder2paths(paths):
    # This is a misnomer. It holds either a list of paths or a string
    # representing a folder from which we glob everything
    if isinstance(paths, str):
        if os.path.isdir(paths):
            import glob
            paths = glob.glob(paths + "/*out")
    assert isinstance(paths, list)
    if len(paths) == 1:
        import glob
        plist = glob.glob("%s/*" % paths[0])
        paths = list(plist)
    fail = False
    for path in paths:
        if not os.path.isfile(path):
            print("path %s is not a file." % path, file=sys.stderr)
            fail = True
    if fail:
        raise Exception("The files are NOT in the computer.")
    return paths


def folder2mashdict(paths):
    paths = folder2paths(paths)
    ret = {}
    for path in paths:
        if "experim" in path:
            ret.update(mash2dict(path))
    return ret


class HlashData:
    def __init__(self, line):
        toks = line.strip().split()
        self.paths = canonname(toks[:2])
        self.est, self.exact = map(float, toks[2:4])
        self.n, self.k = map(int, toks[-2:])
        self.nbytes = 1 << self.n
        self.key = canonkey(self.paths[0], self.paths[1],
                            n=self.nbytes, k=self.k)


def folder2hlashdict(paths):
    paths = folder2paths(paths)
    return {canonkey(x): x for x in map(HlashData,
                                        (line for path in paths if
                                         "genome" in path and "out" in path
                                         for line in open(path) if
                                         line[0] != "#"))}


__all__ = ["mash2dict", "folder2mashdict", "get_flag",
           "folder2hlashdict", "folder2paths",
           "HlashData", "nk2sketchbytes", "canonname", "canonkey"]
