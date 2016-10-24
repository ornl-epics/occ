#!/bin/env python

import argparse
import occ

def main():
    parser = argparse.ArgumentParser(description="Print OCC status")
    parser.add_argument("device", help="Device file name, eg. /dev/snsocb1", default="/dev/snsocb0")
    args = parser.parse_args()

    o = occ.open_debug(args.device)
    status = o.status()
    for k,v in status.iteritems():
        print "{0}: {1}".format(k, v)

if __name__ == "__main__":
    main()
