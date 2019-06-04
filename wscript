# -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-

# def options(opt):
#     pass

# def configure(conf):
#     conf.check_nonfatal(header_name='stdint.h', define_name='HAVE_STDINT_H')

def build(bld):
    module = bld.create_ns3_module('offchain', ['internet','core'])
    module.source = [
        'model/balanceProof.cc',
        'model/neighbors.cc',
        'model/offchain-dpd.cc',
        'model/offchain-id.cc',
        'model/offchain-routing.cc',
        'model/payment-network.cc',
        'model/payroute-packet.cc',
        'model/routemsg-queue.cc',
        ]

    module_test = bld.create_ns3_module_test_library('offchain')
    module_test.source = [
        'test/offchain-test-suite.cc',
        ]

    headers = bld(features='ns3header')
    headers.module = 'offchain'
    headers.source = [
        'model/balanceProof.h',
        'model/neighbors.h',
        'model/offchain-dpd.h',
        'model/offchain-id.h',
        'model/offchain-routing.h',
        'model/payment-network.h',
        'model/payroute-packet.h',
        'model/routemsg-queue.h',
        ]

    if bld.env.ENABLE_EXAMPLES:
        bld.recurse('examples')

    # bld.ns3_python_bindings()

