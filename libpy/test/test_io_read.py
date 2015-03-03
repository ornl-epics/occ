#!/bin/env python

import occ

o = occ.open("/dev/snsocb0")
o.status(fast=True)
o.io_read(bar=0, offset=4)
