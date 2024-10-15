/* $Id: webtest.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * webtest.cpp:
 *      demo webservice client in C++. This mimics some of the
 *      functionality of VBoxManage for testing purposes.
 */
/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

// gSOAP headers (must come after vbox includes because it checks for conflicting defs)
#include "soapStub.h"

// include generated namespaces table
#include "vboxwebsrv.nsmap"

#include <iostream>
#include <iprt/sanitized/sstream>
#include <iprt/sanitized/string>

#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/errcore.h>


static void usage(int exitcode)
{
    std::cout <<
       "webtest: VirtualBox webservice testcase.\n"
       "\nUsage: webtest [options] [command]...\n"
       "\nSupported options:\n"
       " -h: print this help message and exit.\n"
       " -c URL: specify the webservice server URL (default http://localhost:18083/).\n"
       "\nSupported commands:\n"
       " - IWebsessionManager:\n"
       "   - webtest logon <user> <pass>: IWebsessionManager::logon().\n"
       "   - webtest getsession <vboxref>: IWebsessionManager::getSessionObject().\n"
       "   - webtest logoff <vboxref>: IWebsessionManager::logoff().\n"
       " - IVirtualBox:\n"
       "   - webtest version <vboxref>: IVirtualBox::getVersion().\n"
       "   - webtest gethost <vboxref>: IVirtualBox::getHost().\n"
       "   - webtest getpc <vboxref>: IVirtualBox::getPerformanceCollector().\n"
       "   - webtest getmachines <vboxref>: IVirtualBox::getMachines().\n"
       "   - webtest createmachine <vboxref> <settingsPath> <name>: IVirtualBox::createMachine().\n"
       "   - webtest registermachine <vboxref> <machineref>: IVirtualBox::registerMachine().\n"
       " - IHost:\n"
       "   - webtest getdvddrives <hostref>: IHost::getDVDDrives.\n"
       " - IHostDVDDrive:\n"
       "   - webtest getdvdname <dvdref>: IHostDVDDrive::getname.\n"
       " - IMachine:\n"
       "   - webtest getname <machineref>: IMachine::getName().\n"
       "   - webtest getid <machineref>: IMachine::getId().\n"
       "   - webtest getostype <machineref>: IMachine::getGuestOSType().\n"
       "   - webtest savesettings <machineref>: IMachine::saveSettings().\n"
       " - IPerformanceCollector:\n"
       "   - webtest setupmetrics <pcref>: IPerformanceCollector::setupMetrics()\n"
       "   - webtest querymetricsdata <pcref>: IPerformanceCollector::QueryMetricsData()\n"
       " - IVirtualBoxErrorInfo:\n"
       "   - webtest errorinfo <eiref>: various IVirtualBoxErrorInfo getters\n"
       " - All managed object references:\n"
       "   - webtest getif <ref>: report interface of object.\n"
       "   - webtest release <ref>: IUnknown::Release().\n";
    exit(exitcode);
}

/**
 *
 * @param argc
 * @param argv[]
 * @return
 */
int main(int argc, char* argv[])
{
    bool fSSL = false;
    const char *pcszArgEndpoint = "http://localhost:18083/";

    /* SSL callbacks drag in IPRT sem/thread use, so make sure it is ready. */
    int vrc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(vrc))
        return RTMsgInitFailure(vrc);

    int ap;
    for (ap = 1; ap < argc; ap++)
    {
        if (argv[ap][0] == '-')
        {
            if (!strcmp(argv[ap], "-h"))
                usage(0);
            else if (!strcmp(argv[ap], "-c"))
            {
                ap++;
                if (ap >= argc)
                    usage(1);
                pcszArgEndpoint = argv[ap];
                fSSL = !strncmp(pcszArgEndpoint, "https://", 8);
            }
            else
                usage(1);
        }
        else
            break;
    }

    if (argc < 1 + ap)
        usage(1);

#ifdef WITH_OPENSSL
    if (fSSL)
        soap_ssl_init();
#endif /* WITH_OPENSSL */

    struct soap soap; // gSOAP runtime environment
    soap_init(&soap); // initialize runtime environment (only once)
#ifdef WITH_OPENSSL
    // Use SOAP_SSL_NO_AUTHENTICATION here to accept broken server configs.
    // In a real world setup please use at least SOAP_SSL_DEFAULT and provide
    // the necessary CA certificate for validating the server's certificate.
    if (fSSL && soap_ssl_client_context(&soap, SOAP_SSL_NO_AUTHENTICATION | SOAP_TLSv1,
                                        NULL /*clientkey*/, NULL /*password*/,
                                        NULL /*cacert*/, NULL /*capath*/,
                                        NULL /*randfile*/))
    {
        soap_print_fault(&soap, stderr);
        exit(1);
    }
#endif /* WITH_OPENSSL */

    const char *pcszMode = argv[ap];
    int soaprc = SOAP_SVR_FAULT;

    if (!strcmp(pcszMode, "logon"))
    {
        if (argc < 3 + ap)
            std::cout << "Not enough arguments for \"" << pcszMode << "\" mode.\n";
        else
        {
            _vbox__IWebsessionManager_USCORElogon req;
            req.username = argv[ap + 1];
            req.password = argv[ap + 2];
            _vbox__IWebsessionManager_USCORElogonResponse resp;

            if (!(soaprc = soap_call___vbox__IWebsessionManager_USCORElogon(&soap,
                                                            pcszArgEndpoint,
                                                            NULL,
                                                            &req,
                                                            &resp)))
                std::cout << "VirtualBox objref: \"" << resp.returnval << "\"\n";
        }
    }
    else if (!strcmp(pcszMode, "getsession"))
    {
        if (argc < 2 + ap)
            std::cout << "Not enough arguments for \"" << pcszMode << "\" mode.\n";
        else
        {
            _vbox__IWebsessionManager_USCOREgetSessionObject req;
            req.refIVirtualBox = argv[ap + 1];
            _vbox__IWebsessionManager_USCOREgetSessionObjectResponse resp;

            if (!(soaprc = soap_call___vbox__IWebsessionManager_USCOREgetSessionObject(&soap,
                                                            pcszArgEndpoint,
                                                            NULL,
                                                            &req,
                                                            &resp)))
                std::cout << "session: \"" << resp.returnval << "\"\n";
        }
    }
    else if (!strcmp(pcszMode, "logoff"))
    {
        if (argc < 2 + ap)
            std::cout << "Not enough arguments for \"" << pcszMode << "\" mode.\n";
        else
        {
            _vbox__IWebsessionManager_USCORElogoff req;
            req.refIVirtualBox = argv[ap + 1];
            _vbox__IWebsessionManager_USCORElogoffResponse resp;

            if (!(soaprc = soap_call___vbox__IWebsessionManager_USCORElogoff(&soap,
                                                            pcszArgEndpoint,
                                                            NULL,
                                                            &req,
                                                            &resp)))
            {
                ;
            }
        }
    }
    else if (!strcmp(pcszMode, "version"))
    {
        if (argc < 2 + ap)
            std::cout << "Not enough arguments for \"" << pcszMode << "\" mode.\n";
        else
        {
            _vbox__IVirtualBox_USCOREgetVersion req;
            req._USCOREthis = argv[ap + 1];
            _vbox__IVirtualBox_USCOREgetVersionResponse resp;

            if (!(soaprc = soap_call___vbox__IVirtualBox_USCOREgetVersion(&soap,
                                                            pcszArgEndpoint,
                                                            NULL,
                                                            &req,
                                                            &resp)))
                std::cout << "version: \"" << resp.returnval << "\"\n";
        }
    }
    else if (!strcmp(pcszMode, "gethost"))
    {
        if (argc < 2 + ap)
            std::cout << "Not enough arguments for \"" << pcszMode << "\" mode.\n";
        else
        {
            _vbox__IVirtualBox_USCOREgetHost req;
            req._USCOREthis = argv[ap + 1];
            _vbox__IVirtualBox_USCOREgetHostResponse resp;

            if (!(soaprc = soap_call___vbox__IVirtualBox_USCOREgetHost(&soap,
                                                            pcszArgEndpoint,
                                                            NULL,
                                                            &req,
                                                            &resp)))
            {
                std::cout << "Host objref " << resp.returnval << "\n";
            }
        }
    }
    else if (!strcmp(pcszMode, "getpc"))
    {
        if (argc < 2 + ap)
            std::cout << "Not enough arguments for \"" << pcszMode << "\" mode.\n";
        else
        {
            _vbox__IVirtualBox_USCOREgetPerformanceCollector req;
            req._USCOREthis = argv[ap + 1];
            _vbox__IVirtualBox_USCOREgetPerformanceCollectorResponse resp;

            if (!(soaprc = soap_call___vbox__IVirtualBox_USCOREgetPerformanceCollector(&soap,
                                                            pcszArgEndpoint,
                                                            NULL,
                                                            &req,
                                                            &resp)))
            {
                std::cout << "Performance collector objref " << resp.returnval << "\n";
            }
        }
    }
    else if (!strcmp(pcszMode, "getmachines"))
    {
        if (argc < 2 + ap)
            std::cout << "Not enough arguments for \"" << pcszMode << "\" mode.\n";
        else
        {
            _vbox__IVirtualBox_USCOREgetMachines req;
            req._USCOREthis = argv[ap + 1];
            _vbox__IVirtualBox_USCOREgetMachinesResponse resp;

            if (!(soaprc = soap_call___vbox__IVirtualBox_USCOREgetMachines(&soap,
                                                                pcszArgEndpoint,
                                                                NULL,
                                                                &req,
                                                                &resp)))
            {
                size_t c = resp.returnval.size();
                for (size_t i = 0;
                     i < c;
                     ++i)
                {
                    std::cout << "Machine " << i << ": objref " << resp.returnval[i] << "\n";
                }
            }
        }
    }
    else if (!strcmp(pcszMode, "createmachine"))
    {
        if (argc < 4 + ap)
            std::cout << "Not enough arguments for \"" << pcszMode << "\" mode.\n";
        else
        {
            _vbox__IVirtualBox_USCOREcreateMachine req;
            req._USCOREthis = argv[ap + 1];
            req.settingsFile = argv[ap + 2];
            req.name = argv[ap + 3];
            std::cout << "createmachine: settingsFile = \"" << req.settingsFile << "\", name = \"" << req.name << "\"\n";
            _vbox__IVirtualBox_USCOREcreateMachineResponse resp;

            if (!(soaprc = soap_call___vbox__IVirtualBox_USCOREcreateMachine(&soap,
                                                                  pcszArgEndpoint,
                                                                  NULL,
                                                                  &req,
                                                                  &resp)))
                std::cout << "Machine created: managed object reference ID is " << resp.returnval << "\n";
        }
    }
    else if (!strcmp(pcszMode, "registermachine"))
    {
        if (argc < 3 + ap)
            std::cout << "Not enough arguments for \"" << pcszMode << "\" mode.\n";
        else
        {
            _vbox__IVirtualBox_USCOREregisterMachine req;
            req._USCOREthis = argv[ap + 1];
            req.machine = argv[ap + 2];
            _vbox__IVirtualBox_USCOREregisterMachineResponse resp;
            if (!(soaprc = soap_call___vbox__IVirtualBox_USCOREregisterMachine(&soap,
                                                                    pcszArgEndpoint,
                                                                    NULL,
                                                                    &req,
                                                                    &resp)))
                std::cout << "Machine registered.\n";
        }
    }
    else if (!strcmp(pcszMode, "getdvddrives"))
    {
        if (argc < 2 + ap)
            std::cout << "Not enough arguments for \"" << pcszMode << "\" mode.\n";
        else
        {
            _vbox__IHost_USCOREgetDVDDrives req;
            req._USCOREthis = argv[ap + 1];
            _vbox__IHost_USCOREgetDVDDrivesResponse resp;
            if (!(soaprc = soap_call___vbox__IHost_USCOREgetDVDDrives(&soap,
                                                           pcszArgEndpoint,
                                                           NULL,
                                                           &req,
                                                           &resp)))
            {
                size_t c = resp.returnval.size();
                for (size_t i = 0;
                    i < c;
                    ++i)
                {
                    std::cout << "DVD drive " << i << ": objref " << resp.returnval[i] << "\n";
                }
            }
        }
    }
    else if (!strcmp(pcszMode, "getname"))
    {
        if (argc < 2 + ap)
            std::cout << "Not enough arguments for \"" << pcszMode << "\" mode.\n";
        else
        {
            _vbox__IMachine_USCOREgetName req;
            req._USCOREthis = argv[ap + 1];
            _vbox__IMachine_USCOREgetNameResponse resp;
            if (!(soaprc = soap_call___vbox__IMachine_USCOREgetName(&soap,
                                                         pcszArgEndpoint,
                                                         NULL,
                                                         &req,
                                                         &resp)))
                printf("Name is: %s\n", resp.returnval.c_str());
        }
    }
    else if (!strcmp(pcszMode, "getid"))
    {
        if (argc < 2 + ap)
            std::cout << "Not enough arguments for \"" << pcszMode << "\" mode.\n";
        else
        {
            _vbox__IMachine_USCOREgetId req;
            req._USCOREthis = argv[ap + 1];
            _vbox__IMachine_USCOREgetIdResponse resp;
            if (!(soaprc = soap_call___vbox__IMachine_USCOREgetId(&soap,
                                                       pcszArgEndpoint,
                                                       NULL,
                                                       &req,
                                                       &resp)))
                std::cout << "UUID is: " << resp.returnval << "\n";;
        }
    }
    else if (!strcmp(pcszMode, "getostypeid"))
    {
        if (argc < 2 + ap)
            std::cout << "Not enough arguments for \"" << pcszMode << "\" mode.\n";
        else
        {
            _vbox__IMachine_USCOREgetOSTypeId req;
            req._USCOREthis = argv[ap + 1];
            _vbox__IMachine_USCOREgetOSTypeIdResponse resp;
            if (!(soaprc = soap_call___vbox__IMachine_USCOREgetOSTypeId(&soap,
                                                             pcszArgEndpoint,
                                                             NULL,
                                                             &req,
                                                             &resp)))
                std::cout << "Guest OS type is: " << resp.returnval << "\n";
        }
    }
    else if (!strcmp(pcszMode, "savesettings"))
    {
        if (argc < 2 + ap)
            std::cout << "Not enough arguments for \"" << pcszMode << "\" mode.\n";
        else
        {
            _vbox__IMachine_USCOREsaveSettings req;
            req._USCOREthis = argv[ap + 1];
            _vbox__IMachine_USCOREsaveSettingsResponse resp;
            if (!(soaprc = soap_call___vbox__IMachine_USCOREsaveSettings(&soap,
                                                              pcszArgEndpoint,
                                                              NULL,
                                                              &req,
                                                              &resp)))
                std::cout << "Settings saved\n";
        }
    }
    else if (!strcmp(pcszMode, "setupmetrics"))
    {
        if (argc < 2 + ap)
            std::cout << "Not enough arguments for \"" << pcszMode << "\" mode.\n";
        else
        {
            _vbox__IPerformanceCollector_USCOREsetupMetrics req;
            req._USCOREthis = argv[ap + 1];
//             req.metricNames[0] = "*";
//             req.objects
            req.period = 1;     // seconds
            req.count = 100;
            _vbox__IPerformanceCollector_USCOREsetupMetricsResponse resp;
            if (!(soaprc = soap_call___vbox__IPerformanceCollector_USCOREsetupMetrics(&soap,
                                                              pcszArgEndpoint,
                                                              NULL,
                                                              &req,
                                                              &resp)))
            {
                size_t c = resp.returnval.size();
                for (size_t i = 0;
                     i < c;
                     ++i)
                {
                    std::cout << "Metric " << i << ": objref " << resp.returnval[i] << "\n";
                }
            }
        }
    }
    else if (!strcmp(pcszMode, "querymetricsdata"))
    {
        if (argc < 2 + ap)
            std::cout << "Not enough arguments for \"" << pcszMode << "\" mode.\n";
        else
        {
            _vbox__IPerformanceCollector_USCOREqueryMetricsData req;
            req._USCOREthis = argv[ap + 1];
//             req.metricNames[0] = "*";
//             req.objects
            _vbox__IPerformanceCollector_USCOREqueryMetricsDataResponse resp;
            if (!(soaprc = soap_call___vbox__IPerformanceCollector_USCOREqueryMetricsData(&soap,
                                                              pcszArgEndpoint,
                                                              NULL,
                                                              &req,
                                                              &resp)))
            {
                size_t c = resp.returnval.size();
                for (size_t i = 0;
                     i < c;
                     ++i)
                {
                    std::cout << "long " << i << ": " << resp.returnval[i] << "\n";
                }
            }
        }
    }
    else if (!strcmp(pcszMode, "errorinfo"))
    {
        if (argc < 2 + ap)
            std::cout << "Not enough arguments for \"" << pcszMode << "\" mode.\n";
        else
        {
            _vbox__IVirtualBoxErrorInfo_USCOREgetResultCode req;
            req._USCOREthis = argv[ap + 1];
            _vbox__IVirtualBoxErrorInfo_USCOREgetResultCodeResponse resp;
            if (!(soaprc = soap_call___vbox__IVirtualBoxErrorInfo_USCOREgetResultCode(&soap,
                                                                                      pcszArgEndpoint,
                                                                                      NULL,
                                                                                      &req,
                                                                                      &resp)))
            {
                std::cout << "ErrorInfo ResultCode: " << std::hex << resp.returnval << "\n";

                _vbox__IVirtualBoxErrorInfo_USCOREgetText req2;
                req2._USCOREthis = argv[ap + 1];
                _vbox__IVirtualBoxErrorInfo_USCOREgetTextResponse resp2;
                if (!(soaprc = soap_call___vbox__IVirtualBoxErrorInfo_USCOREgetText(&soap,
                                                                                    pcszArgEndpoint,
                                                                                    NULL,
                                                                                    &req2,
                                                                                    &resp2)))
                {
                    std::cout << "ErrorInfo Text:       " << resp2.returnval << "\n";

                    _vbox__IVirtualBoxErrorInfo_USCOREgetNext req3;
                    req3._USCOREthis = argv[ap + 1];
                    _vbox__IVirtualBoxErrorInfo_USCOREgetNextResponse resp3;
                    if (!(soaprc = soap_call___vbox__IVirtualBoxErrorInfo_USCOREgetNext(&soap,
                                                                                        pcszArgEndpoint,
                                                                                        NULL,
                                                                                        &req3,
                                                                                        &resp3)))
                        std::cout << "Next ErrorInfo:       " << resp3.returnval << "\n";
                }
            }
        }
    }
    else if (!strcmp(pcszMode, "release"))
    {
        if (argc < 2 + ap)
            std::cout << "Not enough arguments for \"" << pcszMode << "\" mode.\n";
        else
        {
            _vbox__IManagedObjectRef_USCORErelease req;
            req._USCOREthis = argv[ap + 1];
            _vbox__IManagedObjectRef_USCOREreleaseResponse resp;
            if (!(soaprc = soap_call___vbox__IManagedObjectRef_USCORErelease(&soap,
                                                                  pcszArgEndpoint,
                                                                  NULL,
                                                                  &req,
                                                                  &resp)))
                std::cout << "Managed object reference " << req._USCOREthis << " released.\n";
        }
    }
    else
        std::cout << "Unknown mode parameter \"" << pcszMode << "\".\n";

    if (soaprc)
    {
        if (    (soap.fault)
             && (soap.fault->detail)
           )
        {
            // generic fault message whether the fault is known or not
            std::cerr << "Generic fault message:\n";
            soap_print_fault(&soap, stderr); // display the SOAP fault message on the stderr stream

            if (soap.fault->detail->vbox__InvalidObjectFault)
            {
                std::cerr << "Bad object ID: " << soap.fault->detail->vbox__InvalidObjectFault->badObjectID << "\n";
            }
            else if (soap.fault->detail->vbox__RuntimeFault)
            {
                std::cerr << "Result code:   0x" << std::hex << soap.fault->detail->vbox__RuntimeFault->resultCode << "\n";
                std::cerr << "ErrorInfo:     " << soap.fault->detail->vbox__RuntimeFault->returnval << "\n";
            }
        }
        else
        {
            std::cerr << "Invalid fault data, fault message:\n";
            soap_print_fault(&soap, stderr); // display the SOAP fault message on the stderr stream
        }
    }

    soap_destroy(&soap); // delete deserialized class instances (for C++ only)
    soap_end(&soap); // remove deserialized data and clean up
    soap_done(&soap); // detach the gSOAP environment

    return soaprc;
}

