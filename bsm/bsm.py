import networkx as nx
from networkx.readwrite import json_graph
import json
import ps

import sys
# modified from https://code.google.com/p/data-provenance/source/browse/trunk/SPADE/src/spade/reporter/OpenBSM.java


eventData = {}
DEBUG = False
processVertices = {}
fileVersions = {}
GET_PS_NAME = True
USE_PS = True
node_counter = 0
process_node_map = {}
file_node_map = {}
graph = nx.DiGraph()
pathCount = 0
ps_info = {}
cur_ps_time = 0

def parseEventToken(line):
    global eventData
    global pathCount
    # main loop
    tokens = line.split(',')
    token_type = int(tokens[0])


    if token_type in [20,21,116,121]:
        eventData = {}

        pathCount = 0
        ##record_length = tokens[1]
        ##audit_record_version = tokens[2]
        event_id = tokens[3]
        ##event_id_modifier = tokens[4]
        date_time = tokens[5]
        offset_msec = tokens[6]
        eventData["event_id"] = event_id
        eventData["event_time"] = date_time + offset_msec

        # if event occurred after current date_time, load new ps_data
        if int(date_time) > cur_ps_time:
            loadPidInfo()
    elif token_type in [36, 122, 117, 124]:
        ##user_audit_id = tokens[1]
        euid = tokens[2]
        egid = tokens[3]
        uid = tokens[4]
        gid = tokens[5]
        pid = tokens[6]
        ##sessionid = tokens[7]
        ##deviceid = tokens[8]
        machineid = tokens[9]
        eventData["pid"] = pid
        eventData["uid"] = uid
        eventData["gid"] = gid
        eventData["euid"] = euid
        eventData["egid"] = egid
        eventData["machine_id"] = machineid
    ##elif token_type in [38, 123, 119, 125]:
        ##process_user_audit_id = tokens[1]
        ##process_euid = tokens[2]
        ##process_egid = tokens[3]
        ##process_uid = tokens[4]
        ##process_gid = tokens[5]
        ##process_pid = tokens[6]
        ##process_session_id = tokens[7]
        ##process_device_id = tokens[8]
        ##process_machine_id = tokens[9]


    elif token_type in [39, 114]:
        ##error = tokens[1]
        return_value = tokens[2]
        eventData["return_value"] = return_value

    # removed type 49
    ##elif token_type in [62,115]:
        ##file_access_mode = tokens[1]
        ##owneruid = tokens[2]
        ##ownergid = tokens[3]
        ##filesystemid = tokens[4]
        ##inodeid = tokens[5]
        ##filedeviceid = tokens[6]

    ##elif token_type in [45,113]:
    ##    arg_number = tokens[1]
    ##    arg_value = tokens[2]
    ##    arg_text = tokens[3]
    elif token_type in [35]:
        path = tokens[1]
        eventData["path" + str(pathCount)] = path
        pathCount += 1
    ##elif token_type in [40]:
    ##    text_string = tokens[1]
    ##elif token_type in [128,129]:
    ##    socket_family = tokens[1]
    ##    socket_local_port = tokens[2]
    ##    socket_address = tokens[3]
    ##elif token_type in [82]:
    ##    exit_status = tokens[1]
    ##    exit_value = tokens[2]
    elif token_type in [19]:
        processEvent(eventData)
    elif DEBUG:
        print "failed to parse line of type {}".format(token_type)


def processEvent(eventData):
    event_id = int(eventData["event_id"])
    if "pid" in eventData:
        pid = eventData["pid"]
    elif DEBUG:
        print "no pid on event {}".format(event_id)
        return
    else:
        return
    time = eventData["event_time"]
    checkCurrentProcess()
    thisProcess = processVertices[pid]

    # exit
    if event_id in [1]:
        checkCurrentProcess()
        del processVertices[pid]
        del process_node_map[pid]

    # fork
    if event_id in [2,25,241]:
        checkCurrentProcess()
        childPID = eventData["return_value"]
        childProcess = createProcessVertex(childPID) if USE_PS else None
        if (childProcess == None):
            childProcess = {}
            childProcess["pid"] = childPID
            childProcess["ppid"] = pid
            childProcess["uid"] = eventData["uid"]
            childProcess["gid"] = eventData["gid"]

        putProcessVertex(childProcess)

        processVertices[childPID] = childProcess

        triggeredEdge = WasTriggeredBy(childProcess, thisProcess)
        triggeredEdge["operation"] = "fork"
        triggeredEdge["time"] = time
        putEdge(triggeredEdge)

    # open
    elif event_id in [72]:
        checkCurrentProcess()
        readPath = eventData["path1"] if "path1" in eventData else eventData["path0"]
        put = not readPath.replace("//", "/") in fileVersions.keys()
        readFileArtifact = createFileVertex(readPath, False)
        if (put):
            putFileVertex(readFileArtifact)

        readEdge = Used(thisProcess, readFileArtifact)
        readEdge["time"] = time
        putEdge(readEdge)

    # open with read and creat/write/trunc
    elif event_id in [73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83]:
        checkCurrentProcess()
        writePath = eventData["path1"] if "path1" in eventData else eventData["path0"]
        writeFileArtifact = createFileVertex(writePath, True)
        putFileVertex(writeFileArtifact)
        generatedEdge = WasGeneratedBy(writeFileArtifact, thisProcess)
        generatedEdge["time"] = time
        putEdge(generatedEdge)

    # rename
    elif event_id in [42]:
        checkCurrentProcess()
        fromPath = eventData["path1"]
        toPath = eventData["path2"]

        """
        if (!toPath.startsWith("/")):
            toPath = fromPath.substring(0, fromPath.lastIndexOf("/")) + toPath;
        }
        """
        put = not fromPath.replace("//", "/") in fileVersions.keys()
        fromFileArtifact = createFileVertex(fromPath, False)
        if (put):
            putFileVertex(fromFileArtifact)

        renameReadEdge = Used(thisProcess, fromFileArtifact)
        renameReadEdge["time"] = time
        putEdge(renameReadEdge)
        toFileArtifact = createFileVertex(toPath, True)
        putFileVertex(toFileArtifact)
        renameWriteEdge = WasGeneratedBy(toFileArtifact, thisProcess)
        renameWriteEdge["time"] = time
        putEdge(renameWriteEdge)
        renameEdge = WasDerivedFrom(toFileArtifact, fromFileArtifact)
        renameEdge["operation"] = "rename"
        renameEdge["time"] = time

def putFileVertex(vert):
    global file_node_map, node_counter
    vert["type"] = "file"
    vert["node_number"] = node_counter
    file_node_map[(vert["path"])] = node_counter
    graph.add_node(node_counter)
    # add attrs
    for k in vert:
        graph.node[node_counter][k] = vert[k]
    node_counter += 1
def putProcessVertex(vert):
    global process_node_map, node_counter, graph
    vert["type"] = "process"
    process_node_map[vert["pid"]] = node_counter
    vert["node_number"] = node_counter

    graph.add_node(node_counter)
    # add attrs
    for k in vert:
        graph.node[node_counter][k] = vert[k]
    node_counter += 1


def WasTriggeredBy(child, parent):
    # get node number for process
    child_node_num = process_node_map[child["pid"]]
    parent_node_num = process_node_map[parent["pid"]]
    edge = {}
    edge["from"] = child_node_num
    edge["to"] = parent_node_num
    edge["type"] = "triggered by"
    return edge
def putEdge(edge):
    graph.add_edge(edge["from"], edge["to"])
    graph[edge["from"]][edge["to"]]["type"] = edge["type"]

    # add additional key/value pairs
    for k in edge:
        graph[edge["from"]][edge["to"]][k] = edge[k]


def createFileVertex(path, update):
    fileArtifact = {}
    path = path.replace("//", "/");
    fileArtifact["path"] = path
    filename = path.split("/");
    if (len(filename) > 0):
        fileArtifact["filename"] = filename[len(filename) - 1]

    version = fileVersions[path] if path in fileVersions.keys() else 0
    if update and path.startswith("/") and not path.startswith("/dev/"):
        version += 1

    fileArtifact["version"] = str(version)
    fileVersions[path] = version
    return fileArtifact

def Used(proc, fileVert):
    # get node number for process
    proc_node_num = process_node_map[proc["pid"]]
    file_node_num = file_node_map[fileVert["path"]]
    edge = {}
    edge["from"] = proc_node_num
    edge["to"] = file_node_num
    edge["type"] = "used"

    return edge
def checkCurrentProcess():
    pid = eventData["pid"]
    # Make sure the process that triggered this event has already been added.
    if (not pid in processVertices):
        process = createProcessVertex(pid) if USE_PS else None
        if (process == None):
            process = {}
            process["pid"] = pid
            process["uid"] = eventData["uid"]
            process["gid"] = eventData["gid"]

        putProcessVertex(process)
        processVertices[pid] = process
        ppid = process["ppid"] if "ppid" in process else None
        if ((ppid != None) and ppid in processVertices):
            triggeredEdge = WasTriggeredBy(process, processVertices.get(ppid))
            putEdge(triggeredEdge)

def WasGeneratedBy(file, proc):
    # get node number for process
    proc_node_num = process_node_map[proc["pid"]]
    file_node_num = file_node_map[file["path"]]
    edge = {}
    edge["from"] = proc_node_num
    edge["to"] = file_node_num
    edge["type"] = "generated"

    return edge

def loadPidInfo():
    global ps_info, cur_ps_time
    ps_info, cur_ps_time = ps.next_info()

def getPidInfo(pid):
    print ps_info
    if pid in ps_info:
        return ps_info[pid]
    else:
        return {}

def createProcessVertex(pid):
    processVertex = {}
    if GET_PS_NAME:
        processVertex = getPidInfo(pid)
    processVertex["pid"] = pid

    return processVertex

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print "Usage: python bsm.py bsmfile"
        sys.exit()
    with open(sys.argv[1]) as f:
        for line in f:
            parseEventToken(line)
    data = json_graph.node_link_data(graph)
    s = json.dumps(data)
    print s

