import sys
import datetime
import time
PID_LOC = 15
PPID_LOC = 21
CMD_POS = 84

PID_LOC = 10
CMD_POS = 61

def setup(ps_filename):
    global f,next_date_line
    f = open(ps_filename)
    next_date_line = f.readline().rstrip()
def next_info():
    global next_date_line
    cur_date_line = next_date_line
    timestamp = time.mktime(time.strptime(cur_date_line, "%a %b %d %H:%M:%S %Z %Y"))
    info = {}
    try:
        next(f)
    except StopIteration:
        return {}, 9999999999999
    for line in f:
        if len(line) < 50:
            next_date_line = line.rstrip()
            return info, timestamp
        info_obj = {}
        pid = int(line[PID_LOC:PID_LOC+4].strip())
        #ppid = int(line[PPID_LOC:PPID_LOC+4].strip())
        cmd_line = line[CMD_POS:]

        info_obj["pid"] = pid
        #info_obj["ppid"] = ppid
        info_obj["cmd"] = cmd_line.split()[0]
        info_obj["timestamp"] = timestamp

        info[pid] = info_obj
    return info, timestamp



if __name__ == '__main__':
    print next_info()
    print next_info()
