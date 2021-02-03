# Input haptic pos - zmq subscribe
# Set position of dobot
# Get position of dobot and calculate Position difference
# Output posDiff - znmq publish

# Details of script:
# 	1. Subscribe on socket "tcp://127.0.0.1:5556" to topic "10400"
# 	2. Publish on socketsend "tcp://127.0.0.1:5557" to topic "10203"

from serial.tools import list_ports
import sys
from dobot_optimized import Dobot
from random import randint
import math
import time

# Write all timing data to file "timing_Dobot.csv"
import pandas

import zmq

context = zmq.Context()

# Create socket for sending/publishing posDiff
##
socketPUB = context.socket(zmq.PUB)
socketPUB.bind("tcp://127.0.0.1:5761")
# socketPUB.bind("tcp://10.0.0.1:5760")
topicPUB = "10400"
##

# Create socket for receiving Haptic position input
# from the other side
socketSUB = context.socket(zmq.SUB)
print("Collecting position data from haptic device...")
socketSUB.connect("tcp://127.0.0.1:5740")
# socketSUB.connect("tcp://10.0.0.2:5740")
# Set Topic for getting messages which you are subscribed to
topicSUB = "10005"
# socketSUB.subscribe("")
socketSUB.setsockopt_string(zmq.SUBSCRIBE, topicSUB)

# Connection to Dobot Magician Teleoperator
port = list_ports.comports()[1].device
device = Dobot(port=port, verbose=True)
device.speed(100, 100)

# Get starting position of Dobot
# (xS, yS, zS, rS, j1S, j2S, j3S, j4S) = device.pose()
poseDobotStart = device.pose()
print("Start Position")
# print(f'x:{x} y:{y} z:{z} j1:{j1} j2:{j2} j3:{j3} j4:{j4}')

# The time difference is : 0.20091200700153422
# Test to see that Dobot is working
device.move_to(poseDobotStart[0],
               poseDobotStart[1],
               poseDobotStart[2] + 20,
               poseDobotStart[3],
               wait=False)
device.move_to(poseDobotStart[0],
               poseDobotStart[1],
               poseDobotStart[2],
               poseDobotStart[3],
               wait=True)
# we wait until this movement is done before continuing
time.sleep(1)
import timeit
# starttime1 = timeit.default_timer()
# print("The time difference is :", timeit.default_timer() - starttime1)

Time_Data = pandas.DataFrame([['ReceiveTimeDiff',
                               'ProcessingTimeDiff',
                               'DobotMoveTimeDiff',
                               'EndTimeDiff','rec2','rec3','poshapX','poshapY','poshapZ', 'DobotMoveX', 'DobotMoveY','DobotMoveZ']])
Time_Data.to_csv('timing_Dobot.csv', mode='w', header=False, index=False)

while True:
    try:
        starttime1 = timeit.default_timer()
        # Get Haptic position data
        # print("Tryagain")
        [topic, msg_ID, posHapDmove] = socketSUB.recv_multipart()
        ReceiveTimeDiff = timeit.default_timer() - starttime1
        #print("ReceiveTimeDiff :", ReceiveTimeDiff)

        # Convert bytes received through network into string for processing
        posHapDmove = posHapDmove.decode("utf-8")
        msg_ID = msg_ID.decode("utf-8")
        # print("Received string: %s" % posHapDmove)

        # Split and and get indiviual data as float
        posHapDmove = posHapDmove.split(",")
        posHapDmoveX, posHapDmoveY, posHapDmoveZ  = list(map(float, posHapDmove))

        #print("Received string3: %s ID: %s" % (posHapDmove, msg_ID))

        ReceiveTimeDiff2 = timeit.default_timer() - starttime1
        #print("ReceiveTimeDiff2 :", ReceiveTimeDiff2)

        # Get Dobot position data
        poseDobot = device.pose()
        xD = poseDobot[0]
        yD = poseDobot[1]
        zD = poseDobot[2]
        position = [xD, yD, zD]
        #print("Current Dobot position: %s" % (position))
        ReceiveTimeDiff3 = timeit.default_timer() - starttime1
        #print("ReceiveTimeDiff3 :", ReceiveTimeDiff3)

        # Get Dobot Movement data i.e. How much it has moved from the start
        DobotMoveX = poseDobot[0] - poseDobotStart[0]
        DobotMoveY = poseDobot[1] - poseDobotStart[1]
        DobotMoveZ = poseDobot[2] - poseDobotStart[2]
        #print("Movement: %f %f %f" % (DobotMoveX, DobotMoveY, DobotMoveZ))
        # Get difference in position
        PosDiffX = posHapDmoveX - DobotMoveX
        PosDiffY = posHapDmoveY - DobotMoveY
        PosDiffZ = posHapDmoveZ - DobotMoveZ
        #print("Position Difference: %f %f %f" % (PosDiffX, PosDiffY, PosDiffZ))
        #print("Here2")
        # print(f"[%.3f,%.3f,%.3f,%.3f]\n",xD,yD,zD,rHead)
        MovetoX = poseDobotStart[0] + posHapDmoveX
        MovetoY = poseDobotStart[1] + posHapDmoveY
        MovetoZ = poseDobotStart[2] + posHapDmoveZ
        #print("Move to: [%f %f %f]" % (MovetoX, MovetoY, MovetoZ))
        ProcessingTimeDiff = timeit.default_timer() - starttime1
        #print("ProcessingTimeDiff :", ProcessingTimeDiff)

        # Place limits on Dobot movement
        # Subject to change : X (140,220), Y (-70,70), Z (25,125)
        if ((40<MovetoX<240) and (-90<MovetoY<200) and (0<MovetoZ<140)):
            device.move_to(MovetoX, MovetoY, MovetoZ, poseDobotStart[3], wait=False)
        else:
            print("Dobot Limits breached")

        DobotMoveTimeDiff = timeit.default_timer() - starttime1
        print("DobotMoveTimeDiff :", DobotMoveTimeDiff)

        # Test command
        # device.move_to(posHapDmoveX, posHapDmoveY, posHapDmoveZ, poseDobotStart[3], wait=False)
        # time.sleep(1)
        # PosDiff = randint(0, 10)
        # PosDiff = "Hey"
        PosDiff = math.sqrt(PosDiffX**2 + PosDiffY**2 + PosDiffZ**2)
        PosDiff_b = bytes(str(PosDiff), 'utf-8')
        msg_ID_b = bytes(msg_ID, 'utf-8')
        topicPUB_b = bytes(topicPUB, 'utf-8')
        # socketPUB.send_string("%s %s %s" % (topicPUB, msg_ID, PosDiff))
        socketPUB.send_multipart([topicPUB_b, msg_ID_b, PosDiff_b], zmq.NOBLOCK)

        EndTimeDiff = timeit.default_timer() - starttime1
        #print("EndTimeDiff :", EndTimeDiff)

        Time_Data = pandas.DataFrame([[ReceiveTimeDiff,
                                      ProcessingTimeDiff,
                                      DobotMoveTimeDiff,
                                      EndTimeDiff, ReceiveTimeDiff2, ReceiveTimeDiff3, posHapDmoveX, posHapDmoveY, posHapDmoveZ]])
        Time_Data.to_csv('timing_Dobot.csv', mode='a', header=False, index=False)
        # print("Lets see")
    except KeyboardInterrupt:
        # device.close()
        socketPUB.close()
        socketSUB.close()
        context.term()
        sys.exit(0)
    except:
        pass
