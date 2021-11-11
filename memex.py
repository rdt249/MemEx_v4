# University of Tennessee at Chattanooga
# RES Lab & UTChattSat
# Stephen Lawrence (rdt249@mocs.utc.edu)

############ SETTINGS #####################

# serial settings
serial_device = '/dev/tty.usbserial-AO009A5O' # location of serial device on host computer
baud_rate = 1000000 # serial baud rate

# default group settings
nominal = [3.3,3.3,3.3,3.3] # nominal voltage (by group)
voltage = [0.5,1.0,1.5,2.0] # holding voltage (by group)
pattern = [85,85,85,85] # fill and check pattern (by group)
size = [1,1,1,1] # size of memory (by group) in kB

# default log settings
log_file = 'data/log.csv' # location to save and update log file
log_header = ['Command(str)',
              'NominalA(V)','NominalB(V)','NominalC(V)','NominalD(V)',
              'SizeA(kB)','SizeB(kB)','SizeC(kB)','SizeD(kB)',
              'HoldTime(s)',
              'BiasA(V)','BiasB(V)','BiasC(C)','BiasD(D)',
              'PatternA(dec)','PatternB(dec)','PatternC(dec)','PatternD(dec)',
              'StatusA1(bool)','StatusA2(bool)','StatusA3(bool)','StatusB1(bool)','StatusB2(bool)','StatusB3(bool)',
              'StatusC1(bool)','StatusC2(bool)','StatusC3(bool)','StatusD1(bool)','StatusD2(bool)','StatusD3(bool)',
              'MeanA(#)','MeanB(#)','MeanC(#)','MeanD(#)',
              'FaultsA1(#)','FaultsA2(#)','FaultsA3(#)','FaultsB1(#)','FaultsB2(#)','FaultsB3(#)',
              'FaultsC1(#)','FaultsC2(#)','FaultsC3(#)','FaultsD1(#)','FaultsD2(#)','FaultsD3(#)',
              'Output(file)']

############# NOTES ####################

version = "4.1"
updated = "2021-11-10"
'''
This script can be imported into another python script with the line:
    import memex as x

Then you can use the functions to script your own tests, for example:
    x.status() # get the status of each DUT
    x.fill() # fill the DUTs with a pattern (default 0x55)
    x.hold() # hold the DUTs until user hits ENTER
    x.scan() # scan the DUTs for faults
If using the command line, use SHIFT+ENTER to separate your lines.

You can also change the settings within your test automation:
    x.size = [1,1,1,1] # limit all DUTs to 1 kB
    x.check() # find the address of every fault
All settings reset to the defaults below when you restart your python kernel.
'''

################ INITIALIZATION ####################

from serial import Serial
serial = Serial(serial_device,baud_rate,timeout=60) # create serial device

################ FAULT INJECTION ###################

import numpy as np
def random_scan() :
    size_arr = np.array(size)
    nom_arr = np.array(nominal)
    volt_arr = np.array(voltage)
    mu = size_arr * (nom_arr - volt_arr)
    sigma = mu/10
    faults = [None] * 12
    for i in range(4) :
        for j in range(3) :
            faults[3*i+j] = np.random.normal(mu[i],sigma[i],1).values
    return faults.tolist()
        

################# FUNCTIONS ######################

from datetime import datetime
from os import path
from csv import writer,QUOTE_MINIMAL
def log(data, header, file = "data/log.csv", timestamp = True):
# create or update log given a data row along with headers
    if timestamp : # if timestamp is requested
        now = datetime.now() # get current time
        date = now.strftime('%Y-%m-%d') # format string for date
        time = now.strftime('%H:%M:%S') # format string for time
        header = ['Date(y-m-d)','Time(H:M:S)'] + header # format header
        data = [date,time] + data # format data
    if path.isfile(file) is False: # check if file exists
        with open(file, 'w') as csvfile: # open file in write mode
            filewriter = writer(csvfile,quoting=QUOTE_MINIMAL) # make csv writer
            filewriter.writerow(header) # write column labels
    with open(file,'a') as csvfile: # open file in append mode
        filewriter = writer(csvfile,quoting=QUOTE_MINIMAL) # make csv writer
        filewriter.writerow(data) # write data
    return file # return location of log file

def bytes2bits(data) :
# turn a list of bytes into a list of bits (8x as long)
    result = [] # declare list for results
    for i in range(len(data)) : # for each member of list
        result += [(int(data[i]) >> b) & 1 for b in range(8)][::-1] # find values for all 8 bits
    return result # return list of bits

def menu() :
    serial.write(b"menu\r\n")
    line = serial.readline().decode("ascii")
    while line != ".\n" :
        print(line,end='')
        line = serial.readline().decode("ascii")

def update_settings() :
# update size and pattern settings for the devices
    sizes = (str(size[0])+',')*3 + (str(size[1])+',')*3 + (str(size[2])+',')*3 + (str(size[3])+',')*3
    sizes = (sizes[:-1] + "\r\n").encode("ascii") # add terminator and encoding
    patterns = (str(pattern[0])+',')*3 + (str(pattern[1])+',')*3 + (str(pattern[2])+',')*3 + (str(pattern[3])+',')*3
    patterns = (patterns[:-1] + "\r\n").encode("ascii") # add terminator and encoding
    serial.write(b"size\r\n") # issue size command
    line = serial.readline() # throw this away
    serial.write(sizes) # set sizes
    line = serial.readline() # throw this away
    serial.write(b"pattern\r\n") # issue pattern command
    line = serial.readline() # throw this away
    serial.write(patterns) # set patterns
    line = serial.readline() # throw this away

from numpy import nan
from time import sleep
def status(save_log=True) :
# check status (1 for good, 0 for bad) for each DUT
    update_settings() # update size and pattern settings
    serial.write(b"status\r\n") # issue status command
    line = serial.readline().decode("ascii") # read response
    try :
        awake = line.split(':')[1].split('.')[0].split(',') # extract data from response
    except :
        print(line)
    awake = [int(x) for x in awake] # convert strings to numbers
    if save_log : # if log is requested
                   #command    nominal   size   thold    tvolts     pattern   status    means     faults    file 
        log_data = ['STATUS'] + nominal + size + [None] + [None]*4 + [None]*4 + awake + [None]*4 + [None]*12 +[None]
        log(log_data,log_header,log_file) # update log
    return awake # return list representing which devices are responsive
 
def fill(save_log=True) :
# fill all DUTs with pattern set by memex.pattern
    awake = status(save_log=False) # check status of devices
    serial.write(b"fill\r\n") # issue fill command
    line = serial.readline() # throw this away
    if save_log : # if log is requested
                   #command    nominal   size   thold    tvolts     pattern   status    means     faults    file 
        log_data = ['FILL'] + nominal + size + [None] + [None]*4 + pattern + awake + [None]*4 + [None]*12 +[None]
        log(log_data,log_header,log_file) # update log
    return pattern # return pattern that was written

from time import sleep,time
def hold(t=None,save_log=True) :
# hold all DUTs at voltage set by memex.voltage
    awake = status(save_log=False) # check status of devices
    serial.write(b"hold\r\n") # issue hold command
    line = serial.readline() # throw this away
    print('\tholding @',voltage,'...',end=' ') # print for user
    if t is None : # if time is not specified
        start = time() # track starting time
        input('(press ENTER to return to nominal)') # wait for user
        t = time() - start # calculate hold time
    else :
        sleep(t) # sleep for specified time
        print('done')
    serial.write(b"lift\r\n") # issue lift command
    line = serial.readline() # throw this away
    if save_log : # if log is requested
                   #command    nominal   size   thold    tvolts     pattern   status    means     faults    file 
        log_data = ['HOLD'] + nominal + size + [t] + voltage + [None]*4 + awake + [None]*4 + [None]*12 +[None]
        log(log_data,log_header,log_file) # update log
    return t,voltage # return holding time and voltage

from numpy import nan,nanmean
from warnings import catch_warnings,simplefilter
from time import sleep
def scan(save_log=True) : 
# quickly scan all DUTs, report number of faults (comparing to memex.pattern)
    awake = status(save_log=False) # check status of devices
    serial.write(b"scan\r\n") # issue scan command
    line = serial.readline().decode("ascii") # read response
    line = line.split(':')[1].split('.')[0].split(',') # extract data from response
    faults = [int(x) if y == 1 else nan for x,y in zip(line,awake)] # only record responsive devices
    means = [nan] * 4 # preallocate list for means
    with catch_warnings() : # ignore warnings
        simplefilter("ignore", category=RuntimeWarning) # ignore warnings
        for i in range(4) : # for each group
            means[i] = nanmean(faults[3*i : 3*i+3]) # compute mean faults
    if save_log : # if log is requested
                   #command    nominal   size   thold    tvolts     pattern   status    means     faults    file 
        log_data = ['SCAN'] + nominal + size + [None] + [None]*4 + pattern + awake + means + faults + [None]
        log(log_data,log_header,log_file) # update log
    return faults

from numpy import nan
from pandas import DataFrame
def read(file='data/temp/read.csv',save_log=True) : 
# read the contents of all memories, save to output file
    awake = status(save_log=False) # check status of devices
    serial.write(b"read\r\n") # issue read command
    line = serial.readline().decode("ascii") # throw this away
    data = [nan] * 12 # preallocate list for data
    for i in range(12) : # for each device
        line = serial.readline().decode("ascii") # read response
        line = line.split(';')[0].split(',') # extract data from response
        #print(i,len(line)) # for debugging
        data[i] = [int(x) if awake[i] == 1 else nan for x in line] # only record responsive devices
    line = serial.readline().decode("ascii") # throw this away
    if save_log : # if log is requested
                   #command    nominal   size   thold    tvolts     pattern   status    means     faults    file 
        log_data = ['READ'] + nominal + size + [None] + [None]*4 + [None] + awake + [None]*4 + [None]*12 + [file]
        log(log_data,log_header,log_file) # update log
    DataFrame(data,index=['A1','A2','A3','B1','B2','B3','C1','C2','C3','D1','D2','D3']).to_csv(file) # save dataframe csv
    return data # return collected data

from numpy import nan,nanmean
from warnings import catch_warnings,simplefilter
from pandas import DataFrame
from time import time
def check(file='data/temp/check.csv',save_log=True) :
# check the contents of all memories
    awake = status(save_log=False) # check status of devices
    serial.write(b"check\r\n") # issue check command
    line = serial.readline().decode("ascii") # throw this away
    data = [nan] * 12 # preallocate list for data
    faults = [nan] * 12 # preallocate list for fault counts
    for i in range(12) : # for each device
        line = serial.readline().decode("ascii") # read response
        line = line.split(';')[0].split(',') # extract data from response
        #print(i,len(line)) # for debugging
        data[i] = [int(x) if awake[i] == 1 else nan for x in line] # only record responsive devices
        faults[i] = sum(bytes2bits(data[i])) if awake[i] == 1 else nan # count total faults
    line = serial.readline().decode("ascii") # throw this away
    means = [nan] * 4 # preallocate list for means
    with catch_warnings() : # ignore warnings
        simplefilter("ignore", category=RuntimeWarning) # ignore warnings
        for i in range(4) : # for each group
            means[i] = nanmean(faults[3*i : 3*i+3]) # compute mean
    if save_log : # if log is requested
                   #command    nominal   size   thold    tvolts     pattern   status    means     faults    file 
        log_data = ['CHECK'] + nominal + size + [None] + [None]*4 + pattern + awake + means + faults + [file]
        log(log_data,log_header,log_file) # update log
    DataFrame(data,index=['A1','A2','A3','B1','B2','B3','C1','C2','C3','D1','D2','D3']).to_csv(file) # save dataframe csv
    return faults # return collected data