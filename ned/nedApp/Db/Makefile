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
DB += ned.template
DB += BasePlugin.template
DB += DiscoverPlugin.template
DB += BaseSocketPlugin.template
DB += DspPlugin.template
DB += RocPlugin.template
DB += FemPlugin.template
DB += DumpPlugin.template
DB += StatPlugin.template
DB += RtdlPlugin.template
DB += AdaraPlugin.template

#----------------------------------------------------
# If <anyname>.db template is not named <anyname>*.template add
# <anyname>_template = <templatename>

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE


