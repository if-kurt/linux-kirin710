#!/usr/bin/env python3
# Kara Kutu G62-2: Hi1102 cfg INI'yi C dizisine cevirir (xxd -i esdegeri; 16 bayt/satir).
# Kullanim: python3 -I kk_ini_uret.py <ini> <cikti.h>   (derleme zamani; INI depo disindadir: CONFIG_KK_HI1102_INI)
# Cikti yalniz derleme dizinine yazilir. Calisma aninda dosya OKUNMAZ; ayni girdiden her seferinde ayni bayt.
import hashlib
import sys


def main(argv):
    if len(argv) != 3:
        sys.stderr.write("kullanim: kk_ini_uret.py <ini> <cikti.h>\n")
        return 64
    with open(argv[1], "rb") as f:
        d = f.read()
    if not d:
        sys.stderr.write("kk_ini_uret: INI bos\n")
        return 1
    sha = hashlib.sha256(d).hexdigest()
    out = []
    out.append("/* SPDX-License-Identifier: GPL-2.0 */\n")
    out.append("/* Kara Kutu G62-2: gomulu INI (%d B, sha256 %s); derleme zamaninda depo disindaki INI'den\n" % (len(d), sha))
    out.append(" * kk_ini_uret.py ile uretilir (CONFIG_KK_HI1102_INI). Depoda tutulmaz. Calisma aninda dosya OKUNMAZ. */\n")
    out.append('#define KK_INI_SHA "%s"\n' % sha)
    out.append("static const unsigned char kk_ini_gomulu[%d] = {\n" % len(d))
    for i in range(0, len(d), 16):
        out.append("\t" + ",".join("0x%02x" % b for b in d[i:i + 16]) + ",\n")
    out.append("};\n")
    with open(argv[2], "w") as f:
        f.write("".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
