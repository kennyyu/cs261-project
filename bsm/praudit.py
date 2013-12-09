import sys
import time

# open praudit file for reading
def setup(praudit_filename):
    global f,next_date_line
    f = open(praudit_filename)

    # find first header to get a timestamp
    while True:
        line = f.readline()
        # extract row info
        row = line.split(',')

        # if start of entry, examine
        # XXX: if this is an execve, will lose that pid info
        if row[0] == 'header':
            try:
                timestamp = time.mktime(time.strptime(row[5], "%a %b %d %H:%M:%S %Y"))
                # set last timestamp
                last_timestamp = timestamp
            # if doesnt match, timestamp wasnt in row[5]
            except ValueError:
                pass
            return

def next_info(num=100):
    global last_timestamp
    # prepare results dict
    pid_map = {}

    # open praudit file and read line by line
    num_found = 0
    while True:
        line = f.readline()
        if not line: break

        # extract row info
        row = line.split(',')

        # if start of entry, examine
        if row[0] == 'header':
            # only care about execve for now
            if row[3] == 'execve(2)':
                data = {}
                data['time'] = row[5]
                timestamp = time.mktime(time.strptime(data['time'], "%a %b %d %H:%M:%S %Y"))
                # set last timestamp
                last_timestamp = timestamp
                data['timestamp'] = timestamp
                data['msec'] = row[6].rstrip()

                # read more lines until entry is done
                while True:
                    inner_line = f.readline()
                    if not inner_line: break
                    inner_row = inner_line.split(',')
                    if inner_row[0] == 'trailer': break
                    if inner_row[0] == 'path': data['cmd'] = inner_row[1].rstrip()
                    if inner_row[0] == 'subject':
                        data['pid'] = inner_row[6]
                        data['ppid'] = inner_row[7]

                # store in pid map
                pid_map[data['pid']] = data

                # break if done
                num_found += 1
                if num == num_found:
                    return pid_map, last_timestamp
    return pid_map, 99999999999


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print "Usage: praudit.py file.praudit"
        sys.exit()
    make_pid_dict(sys.argv[1])

