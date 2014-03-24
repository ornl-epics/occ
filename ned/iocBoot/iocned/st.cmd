#!../../bin/linux-x86_64/ned

< envPaths

## Register all support components
dbLoadDatabase("../../dbd/ned.dbd",0,0)
ned_registerRecordDeviceDriver(pdbbase) 

asynSetTraceIOMask("$(PORT)",0,255)

## Load record instances
epicsEnvSet("PREFIX", "SNS:")
epicsEnvSet("PORT",   "/dev/snsocb0")
dbLoadRecords("../../db/ned.db","P=$(PREFIX),R=ocb1:,PORT=$(PORT),ADDR=0,TIMEOUT=1")
nedSetup("$(PORT)", 0, 1000)

dbLoadRecords("$(ASYN)/db/asynRecord.db","P=$(PREFIX):,R=ocb1,PORT=$(PORT),ADDR=0,OMAX=80,IMAX=80")

iocInit()
