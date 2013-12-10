import sys
import time

TYPE_MAPPING = {
        "back": "dos",
        "dict": "r2l",
        "eject": "u2r",
        "ffb": "u2r",
        "format": "u2r",
        "ftp-write": "r2l",
        "guest":  "r2l",
        "imap":   "r2l",
        "ipsweep": "probe",
        "land":    "dos",
        "loadmodule": "u2r",
        "multihop": "r2l",
        "neptune": "dos",
        "nmap": "probe",
        "perlmagic": "u2r",
        "phf": "r2l",
        "pod": "dos",
        "portsweep": "probe",
        "rootkit": "u2r",
        "satan":  "probe",
        "smurf": "dos",
        "spy": "r2l",
        "syslog": "dos",
        "teardrop": "dos",
        "warez":  "r2l",
        "warezclient": "r2l",
        "warezmaster": "r2l"
}

def parse_row(line):
    obj = {}
    row = line.split()
    obj["id"] = row[0]
    full_time_str = row[1] + ' ' + row[2]
    obj["timestamp"] = time.mktime(time.strptime(full_time_str, "%m/%d/%Y %H:%M:%S"))
    obj["name"] = row[4]
    if obj["name"] not in TYPE_MAPPING:
        #print "Unknown type {}".format(obj["name"])
        obj["type"] = "unknown"
    else:
        obj["type"] = TYPE_MAPPING[obj["name"]]

    return obj



def parse_bsm_list(listfile):
    res = []
    with open(listfile) as f:
        for line in f:
            res.append(parse_row(line))
    return res

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print "Usage: python bsm_list.py bsm.list"
        sys.exit()

    print parse_bsm_list(sys.argv[1])

