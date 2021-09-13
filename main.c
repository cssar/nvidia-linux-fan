#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <X11/Xlib.h>
#include <NVCtrl/NVCtrlLib.h>

typedef struct{
    bool fan_stop;
    int refresh_interval;
    int fan_hysteresis;
    int num_points;
    int fan_curve[100];
} fan_profile;


typedef struct{
    int num_fans;
    int curr_temp;
    int last_temp;
    int min_temp;
    int max_temp;
    int curr_duty;
    char* utilization;
} gpu_info;


bool terminate = true;
Display *display;
fan_profile profile;
gpu_info gpu;


int get_num_fans(){
    unsigned char* fan_info;
    XNVCTRLQueryTargetBinaryData (
        display,
        NV_CTRL_TARGET_TYPE_GPU,
        0,
        0,
        NV_CTRL_BINARY_DATA_COOLERS_USED_BY_GPU,
        &fan_info,
        0
    );
    int num_fans = *fan_info;
    free(fan_info);
    return num_fans;
}


int get_temp(){
    int curr_temp;
    XNVCTRLQueryTargetAttribute(
        display,
        NV_CTRL_TARGET_TYPE_GPU,
        0,
        0,
        NV_CTRL_GPU_CORE_TEMPERATURE,
        &curr_temp
        );
    return curr_temp;
}


int get_fan_duty(int fan_index){
    int fan_duty;
    XNVCTRLQueryTargetAttribute(
        display,
        NV_CTRL_TARGET_TYPE_COOLER,
        fan_index,
        0,
        NV_CTRL_THERMAL_COOLER_CURRENT_LEVEL,
        &fan_duty
        );
    return fan_duty;
}


int lookup_new_duty(int temp){
    return profile.fan_curve[temp];
}


void set_fan_duty(int fan_index, int duty){
    XNVCTRLSetTargetAttribute(
        display,
        NV_CTRL_TARGET_TYPE_COOLER,
        fan_index,
        0,
        NV_CTRL_THERMAL_COOLER_LEVEL,
        duty
    );
}

void enable_fan_control(){
    XNVCTRLSetTargetAttribute(
        display,
        NV_CTRL_TARGET_TYPE_GPU,
        0,
        0,
	    NV_CTRL_GPU_COOLER_MANUAL_CONTROL,
        NV_CTRL_GPU_COOLER_MANUAL_CONTROL_TRUE
    );
}
char * get_utilization(){
    char* utilization;
    XNVCTRLQueryTargetStringAttribute(
        display,
        NV_CTRL_TARGET_TYPE_GPU,
        0,
        0,
        NV_CTRL_STRING_GPU_UTILIZATION,
        &utilization
    );
    return utilization;
}

void smooth_curve(int fan_points[2][profile.num_points]){

    int max_temp = fan_points[0][profile.num_points-1];
    int min_temp = fan_points[0][0];
    int min_speed = fan_points[1][0];

    if(!profile.fan_stop){
        for(int i = 0; i < min_temp; i++){
           profile.fan_curve[i] = min_speed;
        }
    }

    for(int i = 0; i < profile.num_points; i++){
        int temp = fan_points[0][i];
        int speed = fan_points[1][i];
        profile.fan_curve[temp] = speed;
    }

    int temp_gap;
    int speed_gap;
    for(int i = 0; i < profile.num_points-1; i++){
        int curr_temp = fan_points[0][i];
        int next_temp= fan_points[0][i+1];
        int temp_gap = next_temp - curr_temp;
        int speed_gap = fan_points[1][i+1] - fan_points[1][i];
        double spacing = speed_gap / (double) temp_gap;
        for(int j = curr_temp, k = 1; j < next_temp; j++, k++){
            profile.fan_curve[j+1] = profile.fan_curve[curr_temp] + (int)(spacing * k);
        }
    }

    for(int i = max_temp + 1; i < 100; i++){
            profile.fan_curve[i + 1] = 100;
    }
}

void read_and_smooth_curve(){
    
    FILE *fan_curve_file = fopen("fan_curve.txt", "r");
    if(!fan_curve_file){
        printf("could not open fan curve file");
        exit(-1);
    }

    int i = 0;
    fscanf(fan_curve_file, "%d", &i);
    profile.fan_stop = i;
    fscanf(fan_curve_file, "%d", &i);
    profile.refresh_interval = i;
    fscanf(fan_curve_file, "%d", &i);
    profile.fan_hysteresis = i;
    fscanf(fan_curve_file, "%d", &i);
    profile.num_points = i;

    int fan_points[2][profile.num_points];
    for(int k = 0; k < profile.num_points; k++){
        fscanf(fan_curve_file, "%d", &i);
        fan_points[0][k] = i;
        fscanf(fan_curve_file, "%d", &i);
        fan_points[1][k] = i;
    }
    smooth_curve(fan_points);
}


void init(){
    display = XOpenDisplay(NULL);
    if(!display){
        printf("\nerror connecting to x11 display");
        exit(-1);
    }
    enable_fan_control();
    read_and_smooth_curve();
    terminate = false;
    gpu.num_fans = get_num_fans();
    gpu.min_temp = 1000;
    gpu.max_temp = -1;
    gpu.utilization = malloc(100);
    printf("\033[A\33[2K\r\n"); // clear console for output
}


void update_info(){
    gpu.curr_temp = get_temp();
    gpu.curr_duty = get_fan_duty(0);
    gpu.max_temp = fmax(gpu.curr_temp, gpu.max_temp);
    gpu.min_temp = fmin(gpu.curr_temp, gpu.min_temp);
    gpu.utilization = get_utilization();
}


void display_info(){
    printf("\33[2K\rTemperature: %d \n\n", gpu.curr_temp);
    printf("\33[2K\rDuty: %d\% \n\n", gpu.curr_duty);
    printf("\33[2K\rUtilization: %s \n\n", gpu.utilization);
    printf("\33[2K\rMin Temperature: %d       Max Temperature: %d \n", gpu.min_temp, gpu.max_temp);
    printf("\033[A\033[A\033[A\033[A\033[A\033[A\033[A"); // clear console for updating
}


void update_fan_duty(){
    if(abs(gpu.last_temp - gpu.curr_temp) >= profile.fan_hysteresis){
        gpu.last_temp = gpu.curr_temp;
        int new_duty = lookup_new_duty(gpu.curr_temp);
        for(int i = 0; i < gpu.num_fans; i++){
            set_fan_duty(i, new_duty);
        }
    }
}

void cleanup(){
    free(gpu.utilization);
}

void finalize(){
    XCloseDisplay(display);
}


int main(){
    init();
    while(!terminate){
        update_info();
        display_info();
        cleanup();
        update_fan_duty();
        sleep(profile.refresh_interval);
     }
    finalize();
    return 0;
}

