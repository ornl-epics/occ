TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

#----------------------------------------------------
#  Optimization of db files using dbst (DEFAULT: NO)
#DB_OPT = YES

#----------------------------------------------------
# Create and install (or just install) into <top>/db
# databases, templates, substitutions like this
DB += Occ.template
DB += BasePlugin.template
DB += BaseSocketPlugin.template
DB += BaseModulePlugin.template
DB += DiscoverPlugin.template
DB += DspPlugin.template
DB += FemPlugin.template
DB += AcpcFemPlugin.template
DB += RocPlugin_v51.template
DB += RocPlugin_v52.template
DB += RocPlugin_v54.template
DB += RB_DspPlugin.template
DB += RB_FemPlugin.template
DB += RB_RocPlugin_v51.template
DB += RB_RocPlugin_v52.template
DB += RB_RocPlugin_v54.template
DB += RocHv.template
DB += ROCHV.db
DB += ROCHVSumBL16b.db
DB += DumpPlugin.template
DB += StatPlugin.template
DB += RtdlPlugin.template
DB += AdaraPlugin.template
DB += CmdPlugin.template
DB += GenericModulePlugin.template

#----------------------------------------------------
# If <anyname>.db template is not named <anyname>*.template add
# <anyname>_template = <templatename>
#ROCHV_template += ../ROCHVCal.template

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
#
test: $(TOP)/db/ROCHV.db
	dbReadTest -I$(TOP)/db -I$(TOP)/dbd -SP=test:area:L -SPORT=any -SADDR=0 -SG=test:area:G ned.dbd ROCHV.db

