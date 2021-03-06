srcdir = '.'
blddir = 'build'
VERSION = '0.0.1'

def set_options(opt):
  opt.tool_options('compiler_cxx')

def configure(conf):
  conf.check_tool('compiler_cxx')
  conf.check_tool('node_addon')

def build(bld):
  obj = bld.new_task_gen('cxx', 'shlib', 'node_addon')
  obj.target = 'eminet'
  obj.source = ['core/EmiNetUtil.cc', 'core/EmiRC4.cc', 'core/EmiConnTime.cc', 'core/EmiMessageHeader.cc', 'core/EmiPacketHeader.cc', 'core/EmiDataArrivalRate.cc', 'core/EmiLossList.cc', 'core/EmiLinkCapacity.cc', 'node/slab_allocator.cc', 'node/eminet.cc', 'node/EmiSocket.cc', 'node/EmiConnection.cc', 'node/EmiConnDelegate.cc', 'node/EmiSockDelegate.cc', 'node/EmiConnectionParams.cc', 'node/EmiError.cc', 'node/EmiNodeUtil.cc', 'node/EmiBinding.cc', 'node/EmiP2PSocket.cc']
