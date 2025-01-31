"""SETUP DAL REPO W/ NFS"""

import geni.portal as portal
import geni.rspec.emulab as emulab
import geni.rspec.pg as pg

pc = portal.Context()

# Define the experiment request
request = pc.makeRequestRSpec()

# Number of nodes
num_nodes = 5  # Change this as needed
hw = "xl170"

# Client image list
imageList = [
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//UBUNTU20-64-STD', 'UBUNTU 20.04'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//UBUNTU18-64-STD', 'UBUNTU 18.04'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//CENTOS8-64-STD', 'CENTOS 8'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//CENTOS7-64-STD', 'CENTOS 7'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//FBSD131-64-STD', 'FreeBSD 13.1'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//FBSD123-64-STD', 'FreeBSD 12.3'),
]

# Server image list, not tested with CentOS
imageList2 = [
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//UBUNTU20-64-STD', 'UBUNTU 20.04'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//UBUNTU18-64-STD', 'UBUNTU 18.04'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//FBSD131-64-STD', 'FreeBSD 13.1'),
    ('urn:publicid:IDN+emulab.net+image+emulab-ops//FBSD123-64-STD', 'FreeBSD 12.3'),
]
nfsServerName = "nfs"
nfsLanName    = "nfsLan"
nfsDirectory  = "/nfs"

# Number of NFS clients (there is always a server)
pc.defineParameter("clientCount", "Number of NFS clients",
                   portal.ParameterType.INTEGER, num_nodes)

pc.defineParameter("osImage", "Select OS image for clients",
                   portal.ParameterType.IMAGE,
                   imageList[1], imageList)

pc.defineParameter("osServerImage", "Select OS image for server",
                   portal.ParameterType.IMAGE,
                   imageList2[1], imageList2)

pc.defineParameter("nfsSize", "Size of NFS Storage",
                   portal.ParameterType.STRING, "80GB",
                   longDescription="Size of disk partition to allocate on NFS server")

pc.defineParameter("hardware", "Node HW",
                   portal.ParameterType.STRING, hw)

# Always need this when using parameters
params = pc.bindParameters()

# The NFS network. All these options are required.
nfsLan = request.LAN(nfsLanName)
nfsLan.best_effort       = True
nfsLan.vlan_tagging      = True
nfsLan.link_multiplexing = True


link_0 = request.Link('link-0')
link_0.Site('undefined')
# The NFS clients, also attached to the NFS lan. Server should also mount NFS
for i in range(0, params.clientCount):
    node = request.RawPC("node-%d" % i)
    if i == 0:
        # The NFS server.
        # nfsServer = request.RawPC(nfsServerName)
        node.disk_image = params.osServerImage
        # Attach server to lan.
        nfsLan.addInterface(node.addInterface())
        # Storage file system goes into a local (ephemeral) blockstore.
        nfsBS = node.Blockstore("nfsBS", nfsDirectory)
        nfsBS.size = params.nfsSize
        iface = node.addInterface('interface-'+str(i))
        link_0.addInterface(iface)
        # Initialization script for the server
        node.addService(pg.Execute(shell="sh", command="sudo /bin/bash /local/repository/nfs-server.sh"))
        # node.addService(pg.Execute(shell="sh", command="sudo /bin/bash /local/repository/nfs-client.sh"))
    else:
        node.hardware_type = hw  # Adjust to your cluster type
        node.routable_control_ip = True
        node.disk_image = params.osImage
        nfsLan.addInterface(node.addInterface())
        iface = node.addInterface('interface-'+str(i))
        link_0.addInterface(iface)
        # Initialization script for the clients
        node.addService(pg.Execute(shell="sh", command="sudo /bin/bash /local/repository/nfs-client.sh"))


# NFS Configuration
# NFS_SERVER_NAME = "node-0"
# NFS_MOUNT_PATH = "/mnt/nfs"

# link_0 = request.Link('link-0')
# link_0.Site('undefined')

# git_command = "git clone https://github.com/mkuerst/DAL.git /mnt/nfs_share/" 

# # Create nodes
# for i in range(num_nodes):
#     node = request.RawPC("node-"+str(i))
#     node.hardware_type = hw  # Adjust to your cluster type

#     node.routable_control_ip = True
#     node.disk_image = 'urn:publicid:IDN+emulab.net+image+emulab-ops//UBUNTU18-64-STD'
#     iface = node.addInterface('interface-'+str(i))
#     link_0.addInterface(iface)

#     # Set the first node as the NFS server
#     if i == 0:
#         node.addService(pg.Execute(shell="bash", command="""
#             sudo apt update && sudo apt install -y nfs-kernel-server git &&
#             sudo mkdir -p /mnt/nfs_share &&
#             sudo chown nobody:nogroup /mnt/nfs_share &&
#             sudo chmod 777 /mnt/nfs_share &&
#             echo "/mnt/nfs_share *(rw,sync,no_subtree_check)" | sudo tee -a /etc/exports &&
#             sudo exportfs -a &&
#             sudo systemctl restart nfs-kernel-server"""))
        
#         node.addService(pg.Execute(shell="bash", command=git_command))

#     # All nodes (including server) mount the NFS share
#     node.addService(pg.Execute(shell="bash", command="""
#         sudo apt update && sudo apt install -y nfs-common git &&
#         sudo mkdir -p /mnt/nfs_client &&
#         sudo mount node-0:/mnt/nfs_share /mnt/nfs_client &&
#         echo "node-0:/mnt/nfs_share /mnt/nfs_client nfs defaults 0 0" | sudo tee -a /etc/fstab
#     """))

# Output the final request
pc.printRequestRSpec(request)