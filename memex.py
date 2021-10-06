########## VERSION 4.0 ####################
# University of Tennessee at Chattanooga
# RES Lab / UTChattSat Club
# Stephen Lawrence (rdt249@mocs.utc.edu)
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
############ SETTINGS #####################

# default group settings
nominal = [3.3,3.3,3.3,3.3] # nominal voltage (by group)
voltage = [0.5,1.0,1.5,2.0] # holding voltage (by group)
pattern = [0x55,0x55,0x55,0x55] # fill and check pattern (by group)
size = [4096,4096,4096,4096] # size of memory (by group) in kB

# default log settings
log_file = 'data/log.csv'
log_header = ['Command(str)',
              'NominalA(V)','NominalB(V)','NominalC(V)','NominalD(V)',
              'SizeA(kB)','SizeB(kB)','SizeC(kB)','SizeD(kB)',
              'HoldTime(s)',
              'HoldVoltsA(V)','HoldVoltsB(V)','HoldVolts(C)','HoldVolts(D)',
              'PatternA(dec)','PatternB(dec)','PatternC(dec)','PatternD(dec)',
              'StatusA1(bool)','StatusA2(bool)','StatusA3(bool)','StatusB1(bool)','StatusB2(bool)','StatusB3(bool)',
              'StatusC1(bool)','StatusC2(bool)','StatusC3(bool)','StatusD1(bool)','StatusD2(bool)','StatusD3(bool)',
              'TotalA(#)','TotalB(#)','TotalC(#)','TotalD(#)',
              'FaultsA1(#)','FaultsA2(#)','FaultsA3(#)','FaultsB1(#)','FaultsB2(#)','FaultsB3(#)',
              'FaultsC1(#)','FaultsC2(#)','FaultsC3(#)','FaultsD1(#)','FaultsD2(#)','FaultsD3(#)',
              'Output(file)']

################# FUNCTIONS ######################

from datetime import datetime
from os import path
from csv import writer,QUOTE_MINIMAL
def log(data, header, file = "data/log.csv", timestamp = True): # create or update log given a data row along with headers
    if timestamp :
        now = datetime.now()
        date = now.strftime('%Y-%m-%d')
        time = now.strftime('%H:%M:%S')
        header = ['Date(y-m-d)','Time(H:M:S)'] + header
        data = [date,time] + data
    if path.isfile(file) is False: # check if file exists
        with open(file, 'w') as csvfile: # open file in write mode
            filewriter = writer(csvfile,quoting=QUOTE_MINIMAL) # make csv writer
            filewriter.writerow(header) # write column labels
    with open(file,'a') as csvfile: # open file in append mode
        filewriter = writer(csvfile,quoting=QUOTE_MINIMAL) # make csv writer
        filewriter.writerow(data) # write data
    return file

def bytes2bits(data) :
    result = []
    for i in range(len(data)) :
        result += [(data[i] >> b) & 1 for b in range(8)][::-1]
    return result

from numpy import nan
def status(save_log=True) : # check status (1 for good, 0 for bad) for each DUT
    awake = [1,1,1,1,1,1,1,1,1,1,1,0]
    if save_log :
                   #command    nominal   size   thold    tvolts     pattern   status    totals     faults    file 
        log_data = ['STATUS'] + nominal + size + [None] + [None]*4 + [None]*4 + awake + [None]*4 + [None]*12 +[None]
        log(log_data,log_header,log_file)
    return awake

def fill() : # fill all DUTs with pattern set by memex.pattern
    awake = status(save_log=False)
               #command    nominal   size   thold    tvolts     pattern   status    totals     faults    file 
    log_data = ['FILL'] + nominal + size + [None] + [None]*4 + pattern + awake + [None]*4 + [None]*12 +[None]
    log(log_data,log_header,log_file)
    return pattern

from time import sleep,time
def hold(t=None) : # hold all DUTs at voltage set by memex.voltage
    awake = status(save_log=False)
    print('\tholding @',voltage,'...')
    if t is None :
        try :
            start = time()
            input('\t(press ENTER to return to nominal)')
            t = time() - start
        except :
            pass
    else :
        sleep(t)
               #command    nominal   size   thold    tvolts     pattern   status    totals     faults    file 
    log_data = ['HOLD'] + nominal + size + [t] + voltage + [None]*4 + awake + [None]*4 + [None]*12 +[None]
    log(log_data,log_header,log_file)
    return t,voltage

from random import random
from numpy import nan
def scan() : # quickly scan all DUTs, report number of faults (comparing to memex.pattern)
    awake = status(save_log=False)
    faults = [nan] * 12
    totals = [nan] * 4
    scale = [50 * (3.3 - v) for v in voltage] # for simulation
    for i in range(4) : # each group
        for j in range(3) : # each member
            if awake[3*i+j] :
                faults[3*i+j] = int(random() * scale[i] + 3*scale[i]) * size[i] # for simulation
        totals[i] = sum(faults[3*i : 3*i+2])
               #command    nominal   size   thold    tvolts     pattern   status    totals     faults    file 
    log_data = ['SCAN'] + nominal + size + [None] + [None]*4 + pattern + awake + totals + faults + [None]
    log(log_data,log_header,log_file)
    return faults

from numpy import nan
from pandas import DataFrame
def save(file='data/temp/save.csv') : # read the contents of all memories, save to output file
    awake = status(save_log=False)
    data = [nan] * 12
    for i in range(4) : # each group
        for j in range(3) : # each member of group
            if awake[3*i+j] :
                temp = []
                for kb in range(size[i]) : # for each kB (refer to memex.size)
                    temp += [pattern[i]] * 1024
                data[3*i+j] = temp
            else :
                data[3*i+j] = [nan] * size[i] * 1024
               #command    nominal   size   thold    tvolts     pattern   status    totals     faults    file 
    log_data = ['SAVE'] + nominal + size + [None] + [None]*4 + [None] + awake + [None]*4 + [None]*12 + [file]
    log(log_data,log_header,log_file)
    DataFrame(data,index=['A1','A2','A3','B1','B2','B3','C1','C2','C3','D1','D2','D3']).to_csv(file)
    return data

from numpy import nan
def check(file='data/temp/check.csv') : # check the contents of all memories
    awake = status(save_log=False)
    faults = [nan] * 12
    totals = [nan] * 4
    data = [nan] * 12
    scale = [100 * (3.3 - v) for v in voltage] # for simulation
    for i in range(4) : # each group
        for j in range(3) : # each member
            if awake[3*i+j] :
                temp = []
                for kb in range(size[i]) : # for each kb in memex.size[i]
                    temp += [int(pattern[i] ^ pattern[i])] * 1024
                data[3*i+j] = temp
                faults[3*i+j] = int(random() * scale[i] + 5*scale[i]) # for simulation
            else :
                data[3*i+j] = [nan] * size[i] * 1024
        totals[i] = sum(faults[3*i : 3*i+2])
               #command    nominal   size   thold    tvolts     pattern   status    totals     faults    file 
    log_data = ['CHECK'] + nominal + size + [None] + [None]*4 + pattern + awake + totals + faults + [file]
    log(log_data,log_header,log_file)
    return data
    