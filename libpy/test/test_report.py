#!/bin/env python

import occ

o = occ.open("/dev/snsocc0")
o.status(fast=True)
with open("/tmp/occ_report.txt", "w") as f:
  o.report(f)
# or simply o.report("/tmp/occ_report.txt")
