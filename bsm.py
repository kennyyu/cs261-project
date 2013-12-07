
# modified from https://code.google.com/p/data-provenance/source/browse/trunk/SPADE/src/spade/reporter/OpenBSM.java


eventData = {}
processVertices = set()
fileVersions = {}
GET_PS_NAME = False
USE_PS = False

def parseEventToken(line):
    # main loop
    tokens = line.split(',')
    token_type = tokens[0]


    if token_type in [20,21,116,121]:
        eventData = {}

        pathCount = 0
        record_length = tokens[1]
        audit_record_version = tokens[2]
        event_id = tokens[3]
        event_id_modifier = tokens[4]
        date_time = tokens[5]
        offset_msec = tokens[6]
        eventData["event_id"] = event_id
        eventData["event_time"] = date_time + offset_msec
    elif token_type in [36, 122, 117, 124]:
        user_audit_id = tokens[1]
        euid = tokens[2]
        egid = tokens[3]
        uid = tokens[4]
        gid = tokens[5]
        pid = tokens[6]
        sessionid = tokens[7]
        deviceid = tokens[8]
        machineid = tokens[9]
        eventData["pid"] = pid
        eventData["uid"] = uid
        eventData["gid"] = gid
        eventData["euid"] = euid
        eventData["egid"] = egid
        eventData["machine_id"] = machineid
    elif token_type in [38, 123, 119, 125]:
        process_user_audit_id = tokens[1]
        process_euid = tokens[2]
        process_egid = tokens[3]
        process_uid = tokens[4]
        process_gid = tokens[5]
        process_pid = tokens[6]
        process_session_id = tokens[7]
        process_device_id = tokens[8]
        process_machine_id = tokens[9]


    elif token_type in [39, 114]:
        error = tokens[1]
        return_value = tokens[2]
        eventData["return_value"] = return_value

    # removed type 49
    elif token_type in [62,115]:
        file_access_mode = tokens[1]
        owneruid = tokens[2]
        ownergid = tokens[3]
        filesystemid = tokens[4]
        inodeid = tokens[5]
        filedeviceid = tokens[6]

    elif token_type in [45,113]:
        arg_number = tokens[1]
        arg_value = tokens[2]
        arg_text = tokens[3]
    elif token_type in [35]:
        path = tokens[1]
        eventData["path" + str(pathCount)] = path
        pathCount += 1
    elif token_type in [40]:
        text_string = tokens[1]
    elif token_type in [128,129]:
        socket_family = tokens[1]
        socket_local_port = tokens[2]
        socket_address = tokens[3]
    elif token_type in [82]:
        exit_status = tokens[1]
        exit_value = tokens[2]
    elif token_type in [19]:
        processEvent(eventData)


def processEvent(eventData):
    pid = eventData["pid"]
    event_id = int(eventData["event_id"])
    time = eventData["event_time"]
    #Process thisProcess = processVertices.get(pid);
    thisProcess = processVertices[pid]
    #boolean put;

    # exit
    if event_id in [1]:
        checkCurrentProcess()
        processVertices.remove(pid)

    # fork
    if event_id in [2,25,241]:
        checkCurrentProcess();
        childPID = eventData["return_value"]
        childProcess = createProcessVertex(childPID) if USE_PS else null
        if (childProcess == null):
            childProcess = {}
            childProcess["pid"] = childPID
            childProcess["ppid"] = pid
            childProcess["uid"] = eventData["uid"]
            childProcess["gid"] = eventData["gid"]

        putVertex(childProcess)

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
        readFileArtifact = createFileVertex(readPath, false)
        if (put):
            putVertex(readFileArtifact)

        readEdge = Used(thisProcess, readFileArtifact)
        readEdge["time"] = time
        putEdge(readEdge)

    # open with read and creat/write/trunc
    elif event_id in [73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83]:
        checkCurrentProcess()
        writePath = eventData["path1"] if "path1" in eventData else eventData["path0"]
        writeFileArtifact = createFileVertex(writePath, true)
        putVertex(writeFileArtifact)
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
        fromFileArtifact = createFileVertex(fromPath, false)
        if (put):
            putVertex(fromFileArtifact)

        renameReadEdge = Used(thisProcess, fromFileArtifact)
        renameReadEdge["time"] = time
        putEdge(renameReadEdge)
        toFileArtifact = createFileVertex(toPath, true)
        putVertex(toFileArtifact)
        renameWriteEdge = WasGeneratedBy(toFileArtifact, thisProcess)
        renameWriteEdge["time"] = time
        putEdge(renameWriteEdge)
        renameEdge = WasDerivedFrom(toFileArtifact, fromFileArtifact)
        renameEdge["operation"] = "rename"
        renameEdge["time"] = time

def putVertex(vert):
    raise NotImplementedError
def WasTriggeredBy(child, parent):
    raise NotImplementedError
def putEdge(edge):
    raise NotImplementedError
def createFileVertex(path, update):
    fileArtifact = {}
    path = path.replace("//", "/");
    fileArtifact["path"] = path
    filename = path.split("/");
    if (filename.length > 0):
        fileArtifact["filename"] = filename[len(filename) - 1]

    version = fileVersions[path] if fileVersions.containsKey(path) else 0
    if update and path.startswith("/") and not path.startswith("/dev/"):
        version += 1

    fileArtifact["version"] = str(version)
    fileVersions[path] = version
    return fileArtifact

def Used(proc, fileVert):
    raise NotImplementedError
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

        putVertex(process)
        processVertices[pid] = process
        ppid = process["ppid"] if "ppid" in process else None
        if ((ppid != None) and ppid in processVertices):
            triggeredEdge = WasTriggeredBy(process, processVertices.get(ppid))
            putEdge(triggeredEdge)

def WasGeneratedBy(file, process):
    raise NotImplementedError
def getPidInfo(pid):
    return {}
"""
    info = line.trim().split("\\s+", 12);
    processVertex.addAnnotation("pidname", info[11]);
    processVertex.addAnnotation("ppid", info[1]);
    String timestring = info[5] + " " + info[6] + " " + info[7] + " " + info[8] + " " + info[9];
    Long unixtime = new java.text.SimpleDateFormat(simpleDatePattern).parse(timestring).getTime();
    String starttime_unix = Long.toString(unixtime);
    String starttime_simple = new java.text.SimpleDateFormat(simpleDatePattern).format(new java.util.Date(unixtime));
    processVertex.addAnnotation("starttime_unix", starttime_unix);
    processVertex.addAnnotation("starttime_simple", starttime_simple);
    processVertex.addAnnotation("user", info[3]);
    processVertex.addAnnotation("uid", info[2]);
    processVertex.addAnnotation("gid", info[4]);
"""

def createProcessVertex(pid):
    processVertex = {}
    if GET_PS_NAME:
        processVertex = getPidInfo(pid)
    processVertex["pid"] = pid

    return processVertex

