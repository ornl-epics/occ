#!/bin/env python

import occ
o = occ.open("/dev/snsocc0")
o.enable_rx()

f = open("/tmp/raw", "r")
o.send(f.read())

data = o.read(timeout=1.0)
f = open("/tmp/raw.out", "w")
f.write(data)
