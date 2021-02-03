#include <stdio.h>
#include <assert.h>
#include "conio.h"
#include <ctype.h>

#include <HD/hd.h>
#include <HDU/hduVector.h>
#include <HDU/hduError.h>

#include <string.h>

/* Socket connection setup */
#include "zhelpers_modified.h"
#include <stdlib.h>

/* sin cos functions */
#include <math.h>
#define PI 3.14159265

/* Mode definitions */
#define MODE_CALIBRATION 0
#define MODE_MOVE 1
#define MODE_END 2

/* Initialize the mode as mode_calibration */
int mode = MODE_CALIBRATION;

/* Holds data retrieved from HDAPI. */
typedef struct 
{
    HDboolean m_buttonState;       /* Has the device button has been pressed. */
    hduVector3Dd m_devicePosition; /* Current device coordinates. */
    HDErrorInfo m_error;
} DeviceData;

static DeviceData gServoDeviceData;

/* Vibration parameters */
static HDint gVibrationFreq = 100; /* Hz */
static HDdouble gVibrationAmplitude = 0; /* N */

/* Callback handle for Vibration */
HDSchedulerHandle gCallbackHandle = 0;

HDCallbackCode HDCALLBACK SetVibrationFreqCallback(void *pUserData);
HDCallbackCode HDCALLBACK SetVibrationAmplitudeCallback(void *pUserData);
HDCallbackCode HDCALLBACK VibrationCallback(void *pUserData);

/******************************************************************************
 Gets the position and sets the force. BeginFrame() and EndFrame() here.
 Simulates a vibration using a sinusoidal wave along the Y axis.
******************************************************************************/
HDCallbackCode HDCALLBACK VibrationCallback(void *pUserData)
{
    static const hduVector3Dd direction = { 0, 1, 0 };
    HDErrorInfo error;
    hduVector3Dd force;
    HDdouble instRate;
    static HDdouble timer = 0;

    /* Variable for storing the button value */
    int nButtons = 0;


    hdBeginFrame(hdGetCurrentDevice());

    /* Force related functions */
    /* Use the reciprocal of the instantaneous rate as a timer. */
    hdGetDoublev(HD_INSTANTANEOUS_UPDATE_RATE, &instRate);
    timer += 1.0 / instRate;

    /* Apply a sinusoidal force in the direction of motion. */
    hduVecScale(force, direction, gVibrationAmplitude * sin(timer * gVibrationFreq));
    
    hdSetDoublev(HD_CURRENT_FORCE, force);
    /* Force related functions */

    /* Position related functions */
    /* Retrieve the current button(s). */
    hdGetIntegerv(HD_CURRENT_BUTTONS, &nButtons);    
    /* Position related functions */

    /* In order to get the specific button 1 state, we use a bitmask to
       test for the HD_DEVICE_BUTTON_1 bit. */
    gServoDeviceData.m_buttonState = 
        (nButtons & HD_DEVICE_BUTTON_1) ? HD_TRUE : HD_FALSE;
        
    /* Get the current location of the device (HD_GET_CURRENT_POSITION)
       We declare a vector of three doubles since hdGetDoublev returns 
       the information in a vector of size 3. */
    hdGetDoublev(HD_CURRENT_POSITION, gServoDeviceData.m_devicePosition);

    /* Also check the error state of HDAPI. */
    gServoDeviceData.m_error = hdGetError();

    /* Copy the position into our device_data tructure. 
       And set the force values */
    hdEndFrame(hdGetCurrentDevice());

    /* Check if an error occurred while attempting to render the force. */
    if (HD_DEVICE_ERROR(error = hdGetError()))
    {
        hduPrintError(stderr, &error, 
                      "Error detected during scheduler callback.\n");

        if (hduIsSchedulerError(&error))
        {
            return HD_CALLBACK_DONE;
        }
    }

    return HD_CALLBACK_CONTINUE;
}

/******************************************************************************
 Modifies the vibration frequency being used by the haptic thread.
******************************************************************************/
HDCallbackCode HDCALLBACK SetVibrationFreqCallback(void *pUserData)
{
    HDint *nFrequency = (HDint *) pUserData;
        
    gVibrationFreq = *nFrequency;        

    return HD_CALLBACK_DONE;
}

/******************************************************************************
 Modifies the vibration amplitude being used by the haptic thread.
******************************************************************************/
HDCallbackCode HDCALLBACK SetVibrationAmplitudeCallback(void *pUserData)
{
    HDdouble *amplitude = (HDdouble *) pUserData;
        
    gVibrationAmplitude = *amplitude;        

    return HD_CALLBACK_DONE;
}

/*****************************************************************************/

/*******************************************************************************
 Checks the state of the gimbal button and gets the position of the device.
*******************************************************************************/
HDCallbackCode HDCALLBACK copyDeviceDataCallback(void *pUserData)
{
    DeviceData *pDeviceData = (DeviceData *) pUserData;

    memcpy(pDeviceData, &gServoDeviceData, sizeof(DeviceData));

    return HD_CALLBACK_DONE;
}

/******************************************************************************
 Runs the main loop of the application.  Queries for keypresses to control
 the frequency of the vibration effect.
******************************************************************************/
void mainLoop()
{
    /* Vibration settings */
    HDint nFrequency = gVibrationFreq;
    HDdouble amplitude;
    HDdouble maxAmplitude;

    /* Button state settings */
    static const int kTerminateCount = 1000;
    int buttonHoldCount = 0;

    /* Instantiate the structure used to capture data from the device. */
    DeviceData currentData;
    DeviceData prevData;

    /* Store starting position in this variable structure */
    static DeviceData startPos; 

    /* Distance of haptic tip position from starting point */
    double distance = 0;

    double moveHX = 0, moveHY = 0, moveHZ = 0;
    double moveDX = 0, moveDY = 0, moveDZ = 0;
    double degrees_to_rad;
    degrees_to_rad = PI/180;
    unsigned long int ID = 0;
    char ID_str[16];


    // Define clock timestamp
    int64_t start_time = 0, send_time = 0, receive_time = 0;

    /* Perform a synchronous call to copy the most current device state. */
    hdScheduleSynchronous(copyDeviceDataCallback, 
        &currentData, HD_MIN_SCHEDULER_PRIORITY);

    memcpy(&prevData, &currentData, sizeof(DeviceData));

    /* Start the amplitude at a fraction of the max continuous 
       force value.  Remember the max continuous force value to be
       used as the max amplitude. This cap prevents the user
       from increasing the amplitude to dangerous limits. */
    hdGetDoublev(HD_NOMINAL_MAX_CONTINUOUS_FORCE, &amplitude);
    maxAmplitude = amplitude;
    amplitude *= 0.75;
    //gVibrationAmplitude = amplitude;
    /* Initialize force by giving 0 force */
    gVibrationAmplitude = 0;

    
    /* Setup connection */
    printf("Connection setup\n");
        //  Prepare our context and publisher
    void *context = zmq_ctx_new ();
    void *publisher = zmq_socket (context, ZMQ_PUB);
    zmq_bind (publisher, "tcp://127.0.0.1:5740");
    printf("Pub created\n");

        // Prepare subscriber
    void *subscriber2 = zmq_socket (context, ZMQ_SUB);
    zmq_connect (subscriber2, "tcp://localhost:5761");
    zmq_setsockopt (subscriber2, ZMQ_SUBSCRIBE, "10400", 5);
    printf("Sub2 created\n");  
    /*---------------------*/

    // Create file and open it
    FILE *fpt;
    fpt = fopen("Haptic_Device_Timing.csv", "w+");
    fprintf(fpt, "ID, HapPosX, HapPosY, HapPosZ, time_send, ID_recv, PosDiff, time_receive, timeD\n");

    /* Run the main loop until the a keyboard button is pressed. */
    while(!_kbhit())
    {
        /* Perform a synchronous call to copy the most current device state.
           This synchronous scheduler call ensures that the device state
           is obtained in a thread-safe manner. */
        hdScheduleSynchronous(copyDeviceDataCallback,
                              &currentData,
                              HD_MIN_SCHEDULER_PRIORITY);


        if (mode == MODE_CALIBRATION)
        {
            /* If the user depresses the gimbal button, save the current 
            position as start position and change mode to MODE_MOVE */
            while (currentData.m_buttonState)
            {
                /* Keep track of how long the user has been pressing the button.
                If this exceeds N ticks, save the current 
                position as start position and 
                then change mode to MODE_MOVE. */
                buttonHoldCount++; 
                if (buttonHoldCount > kTerminateCount)
                {
                    /* Store off the current data as the start position. */
                    memcpy(&startPos, &currentData, sizeof(DeviceData));                     
                    printf("Start position\n");
                    printf("Starting at %g,%g,%g\n",
                        startPos.m_devicePosition[0],
                        startPos.m_devicePosition[1],
                        startPos.m_devicePosition[2]);
                    mode = MODE_MOVE;
                    start_time = s_clock();
                    break;
                }          
                // fprintf(stdout, "Current position: (%g, %g, %g)\n", 
                //     currentData.m_devicePosition[0], 
                //     currentData.m_devicePosition[1], 
                //     currentData.m_devicePosition[2]); 
                else if (!currentData.m_buttonState && prevData.m_buttonState)
                {
                    /* Reset the button hold count, since the user stopped holding
                    down the stylus button. */
                    buttonHoldCount = 0;
                }           
            }
        }
        else if (mode == MODE_MOVE)
        {
            // Compute distance between start and current position
            distance = pow(currentData.m_devicePosition[0] - startPos.m_devicePosition[0], 2) +
                pow(currentData.m_devicePosition[1] - startPos.m_devicePosition[1], 2) +
                pow(currentData.m_devicePosition[2] - startPos.m_devicePosition[2], 2);
            distance = sqrt(distance);
            if (distance > 150.0) { mode = MODE_END; }
            fprintf(stdout, "Current position: (%g, %g, %g)\n", 
                currentData.m_devicePosition[0], 
                currentData.m_devicePosition[1], 
                currentData.m_devicePosition[2]);
                        
//            if (continueexperiment % 2000 == 0)
//            {
//                printf("%f\n", distance);
//            }
            //printf("Device position: %.3f %.3f %.3f\n",
            //position[0], position[1], position[2]);
            //s_sleep(50);
            /* Send position data */
            /* Modify the position data to match Linux Dobot X,Y,Z */
            /* 1st option: Y to Z
                X to X
                Z to -Y
               2nd option: Y to Z
                X to +X,+Y
                Z to +X,-Y
            */
            char position_send[256];
            moveHX = currentData.m_devicePosition[0] - startPos.m_devicePosition[0];
            moveHY = currentData.m_devicePosition[1] - startPos.m_devicePosition[1];
            moveHZ = currentData.m_devicePosition[2] - startPos.m_devicePosition[2];

            moveDZ = moveHY;
            // moveDX_1 = pow(-moveHX,2) + pow(moveHZ,2);
            // moveDX = sqrt(moveDX_1)

            moveDX = moveHX*cos(45.0*degrees_to_rad) + moveHZ*sin(45.0*degrees_to_rad);
            //moveDY_1 = pow(moveHX,2) + pow(moveHZ,2);
            //moveDY = sqrt(moveDY_1)
            moveDY = moveHX*sin(45.0*degrees_to_rad) - moveHZ*cos(45.0*degrees_to_rad);
            send_time = s_clock();
            sprintf(position_send,"%f,%f,%f",moveDX,moveDY,moveDZ);
            printf("%s  -- %lu @ %ld\n", position_send, ID, (send_time - start_time));
            //  Write two messages, each with an envelope and content
            // printf("Sending message\n");

            sprintf(ID_str, "%lu", ID);
            //fprintf(fpt, "%s,%s,%ld\n",  ID_str, position_send, (send_time - start_time));

            s_sendmore (publisher, "A");
            s_sendmore (publisher, ID_str);
            s_send (publisher, "We don't want to see this");
            s_sendmore (publisher, "10005");
            s_sendmore (publisher, ID_str);
            s_send (publisher, position_send);
            //s_sleep(1);

            ID += 1;
            //  Read envelope with address
            char *address2 = s_recv (subscriber2);
            //printf("receive message\n");
            //  Read message contents
            char *msg_ID = s_recv (subscriber2);
            char *contents2 = s_recv (subscriber2);
            if (contents2!=NULL) {
                float contents2_f = atof(contents2);
                receive_time = s_clock();
                printf("Received: ID: %s PosDiff: %g @ %ld\n", msg_ID, contents2_f, (receive_time - start_time));
                fprintf(fpt, "%s,%s,%ld,%s,%s,%ld,%ld\n",  ID_str, position_send, (send_time - start_time), msg_ID, contents2, (receive_time - start_time), (receive_time - send_time));

                /* Set vibration force according to the position difference */
                if (contents2_f < 100.0)
                {
                    amplitude = (contents2_f/100.0) * maxAmplitude;
                }
                else if (contents2_f >= 100.0)
                {
                    amplitude = maxAmplitude;
                }
                hdScheduleSynchronous(SetVibrationAmplitudeCallback, &amplitude,
                                      HD_DEFAULT_SCHEDULER_PRIORITY);
            }

            //printf ("[%s] %s\n", address2, contents2);
            // printf ("%s\n", contents2);
            free (address2);
            free (contents2);
            //s_sleep(1000);
        }
        else if (mode == MODE_END)
        {
            printf("\n\nYou have successfully completed the task.\nPut the stylus back into the inkwell.\n");
            break;
        }
        // /* If the user depresses the gimbal button, display the current 
        //    location information. */
        // if (currentData.m_buttonState && !prevData.m_buttonState)
        // {           
        //     fprintf(stdout, "Current position: (%g, %g, %g)\n", 
        //         currentData.m_devicePosition[0], 
        //         currentData.m_devicePosition[1], 
        //         currentData.m_devicePosition[2]); 
        // }
        // else if (currentData.m_buttonState && prevData.m_buttonState)
        // {
        //     /* Keep track of how long the user has been pressing the button.
        //        If this exceeds N ticks, then terminate the application. */
        //     buttonHoldCount++;

        //     if (buttonHoldCount > kTerminateCount)
        //     {
        //         /* Quit, since the user held the button longer than
        //            the terminate count. */
        //         break;
        //     }
        // }
        // else if (!currentData.m_buttonState && prevData.m_buttonState)
        // {
        //     /* Reset the button hold count, since the user stopped holding
        //        down the stylus button. */
        //     buttonHoldCount = 0;
        // }

        /* Check if the main scheduler callback has exited. */
        if (!hdWaitForCompletion(gCallbackHandle, HD_WAIT_CHECK_STATUS))
        {
            fprintf(stderr, "\nThe main scheduler callback has exited\n");
            fprintf(stderr, "\nPress any key to quit.\n");
            getch();
            return;
        }

        /* Check if an error occurred. */
        if (HD_DEVICE_ERROR(currentData.m_error))
        {
            hduPrintError(stderr, &currentData.m_error, "Device error detected");

            if (hduIsSchedulerError(&currentData.m_error))
            {
                /* Quit, since communication with the device was disrupted. */
                fprintf(stderr, "\nPress any key to quit.\n");
                getch();                
                break;
            }
        }

        /* Store off the current data for the next loop. */
        memcpy(&prevData, &currentData, sizeof(DeviceData));        
    }

    fclose(fpt);


    /* Terminate the connections */
    printf("Connection terminate\n");
    // clean up socket connections
    zmq_close (publisher);
    zmq_close (subscriber2);
    zmq_ctx_destroy (context); 
}   

/******************************************************************************
 Initiates a vibration effect and get position
 from the main loop.
******************************************************************************/
int main(int argc, char* argv[])
{  
    HDErrorInfo error;
    
    HHD hHD = hdInitDevice(HD_DEFAULT_DEVICE);
    if (HD_DEVICE_ERROR(error = hdGetError()))
    {
        hduPrintError(stderr, &error, "Failed to initialize haptic device");
        fprintf(stderr, "\nPress any key to quit.\n");
        getch();
        return -1;
    }

    printf("Force-feedback Effect Example\n");
    printf("Found %s.\n\n", hdGetString(HD_DEVICE_MODEL_TYPE));

    gCallbackHandle = hdScheduleAsynchronous(
        VibrationCallback, 0, HD_MAX_SCHEDULER_PRIORITY);

    hdEnable(HD_FORCE_OUTPUT);
    hdStartScheduler();
    if (HD_DEVICE_ERROR(error = hdGetError()))
    {
        hduPrintError(stderr, &error, "Failed to start scheduler");
        fprintf(stderr, "\nPress any key to quit.\n");
        getch();
        return -1;
    }

    mainLoop();

    hdStopScheduler();
    hdUnschedule(gCallbackHandle);
    hdDisableDevice(hHD);

    return 0;
}