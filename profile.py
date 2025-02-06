"""SETUP DAL REPO W/ NFS"""

import geni.portal as portal
import geni.rspec.emulab as emulab
import geni.rspec.pg as pg

pc = portal.Context()

request = pc.makeRequestRSpec()

num_nodes = 5
hw = "r6525"

imageList = [
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//UBUNTU20-64-STD', 'UBUNTU 20.04'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//UBUNTU22-64-STD', 'UBUNTU 22.04'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//UBUNTU18-64-STD', 'UBUNTU 18.04'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//CENTOS8-64-STD', 'CENTOS 8'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//CENTOS7-64-STD', 'CENTOS 7'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//FBSD131-64-STD', 'FreeBSD 13.1'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//FBSD123-64-STD', 'FreeBSD 12.3'),
]

imageList2 = [
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//UBUNTU20-64-STD', 'UBUNTU 20.04'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//UBUNTU22-64-STD', 'UBUNTU 22.04'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//UBUNTU18-64-STD', 'UBUNTU 18.04'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//FBSD131-64-STD', 'FreeBSD 13.1'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//FBSD123-64-STD', 'FreeBSD 12.3'),
]
nfsServerName = "nfs"
nfsLanName    = "nfsLan"
nfsDirectory  = "/nfs"

pc.defineParameter("clientCount", "Number of nodes",
                   portal.ParameterType.INTEGER, num_nodes)

pc.defineParameter("osImage", "Select OS image for clients",
                   portal.ParameterType.IMAGE,
                   imageList[0], imageList)

pc.defineParameter("osServerImage", "Select OS image for server",
                   portal.ParameterType.IMAGE,
                   imageList2[0], imageList2)

pc.defineParameter("nfsSize", "Size of NFS Storage",
                   portal.ParameterType.STRING, "80GB",
                   longDescription="Size of disk partition to allocate on NFS server")

pc.defineParameter("hardware", "Node HW",
                   portal.ParameterType.STRING, hw)

params = pc.bindParameters()

nfsLan = request.LAN(nfsLanName)
nfsLan.best_effort       = True
nfsLan.vlan_tagging      = True
nfsLan.link_multiplexing = True


ips1 = ["10.10.1.%d" % i for i in range(0, num_nodes+1)]
ips2 = ["10.10.2.%d" % i for i in range(0, num_nodes+1)]
link_0 = request.Link('link-0')
link_0.Site('undefined')

for i in range(1, params.clientCount+1):
    node = request.RawPC("node%d" % i)
    node.hardware_type = params.hardware
    node.routable_control_ip = True

    node.disk_image = params.osImage
    iface_nfs = node.addInterface()  
    iface_nfs.addAddress(pg.IPv4Address(ips1[i], "255.255.255.0"))
    nfsLan.addInterface(iface_nfs)  # Add to nfsLan

    
    iface_link = node.addInterface("node%d" % i)  
    iface_link.addAddress(pg.IPv4Address(ips2[i], "255.255.255.0"))
    
    link_0.addInterface(iface_link)  # Add to link_0

    if i == 1:
        nfsBS = node.Blockstore("nfsBS", nfsDirectory)
        nfsBS.size = params.nfsSize
        node.addService(pg.Execute(shell="sh", command="sudo /bin/bash /local/repository/nfs-server.sh"))
    else:
        node.addService(pg.Execute(shell="sh", command="sudo /bin/bash /local/repository/nfs-client.sh"))

    node.installRootKeys(True, True)
    # node.addService(pg.Execute(shell="sh", command="sudo /bin/bash /local/repository/installLibs.sh"))

pc.printRequestRSpec(request)