/**
 * Copyright (c), 2012~2020 iot.10086.cn All Rights Reserved
 *
 * @file main.c
 * @brief
 */

/*****************************************************************************/
/* Includes                                                                  */
/*****************************************************************************/
#include "iot_api.h"
#include "log.h"
#include "plat_osl.h"
#include "plat_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <linux/input.h>
#include <fcntl.h>

#include <stdbool.h>
#include <sys/socket.h>
#include <arpa/inet.h>  //提供专属结构体的头文件和 htons
#include <netinet/in.h> //提供宏定义 INADDR_ANY
#include <pthread.h>

//=========全局变量的定义==========
int lcd, ts;
int *mmap_star;
struct input_event touch;
int touch_x, touch_y;
int mqtts_flag = 0;
int num111; // 温度随机值的存放

#define MSG_LEN 50        // 信息缓冲区的长度
#define IP_LEN 20         // ip地址的长度
#define SERVICE_PROT 8888 // 服务器的端口
#define CLIENT_NUM 1000   // 用户端的数量

/*客户端的结构体*/
typedef struct client_list
{
    int c_fd;                                    // 懂得都懂
    char ip[IP_LEN];                             // 存ip地址
    struct client_list *next, *client_list_head; // 存头节点
} CL, *P_CL;

/*服务器的结构体*/
typedef struct service_inf
{
    int s_fd;                    // 懂得都懂
    int client_num;              // 用户端的实时数量
    pthread_t w_id;              // 服务器等待客户端和键盘接收的线程
    struct sockaddr_in s_inf;    // 懂得都懂
    pthread_t c_pid[CLIENT_NUM]; // 每个用户端的收发线程
    P_CL client_list_head;       // 存放客户端链表头

} SI, *P_SI;

P_SI p_si;
//==========函数的声明==========
int init();                   // 初始化
int show_bmp(char *bmp_path); // 显示单张图片
int pic();                    // 可以切换多张图片
int touch_xy();               // 获取触摸屏坐标
void *mqtt_Send_Msg(void *arg);

int free_init(); // 释放内存

P_SI Service_Init();

void *Wait_For_Client(void *arg);
void *Service_Broadcast(void *arg);
int Service_Working(P_SI p_si);
P_CL Creta_List_Node();
int Del_Client_Node(P_CL head, P_CL del_node);
int Service_Free(P_SI p_si);
void *random_num(void *arg);
//==========函数的定义==========

P_SI Service_Init() // 服务器初始化
{
    // malloc申请堆空间
    P_SI p_si = (P_SI)malloc(sizeof(SI));
    if (p_si == NULL)
    {
        perror("malloc");
        return (P_SI)-1;
    }

    memset(p_si, 0, sizeof(SI));

    p_si->client_num = 0; // 初始化客户的数量为0

    // 创建套接字
    p_si->s_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (p_si->s_fd == -1)
    {
        perror("socket");
        return (P_SI)-1;
    }
    printf("%d\n", p_si->s_fd);

    p_si->s_inf.sin_port = htons(SERVICE_PROT);
    p_si->s_inf.sin_family = AF_INET;
    p_si->s_inf.sin_addr.s_addr = htonl(INADDR_ANY);

    int bind_ret = bind(p_si->s_fd, (struct sockaddr *)&p_si->s_inf, sizeof(p_si->s_inf));
    if (bind_ret == -1)
    {
        perror("bind");
        return (P_SI)-1;
    }

    int listen_ret = listen(p_si->s_fd, 30);
    if (listen_ret == -1)
    {
        perror("listen");
        return (P_SI)-1;
    }

    // 创建客户端链表头

    p_si->client_list_head = (P_CL)malloc(sizeof(CL));
    if (p_si->client_list_head == NULL)
    {
        perror("malloc");
        return (P_SI)-1;
    }
    memset(p_si->client_list_head, 0, sizeof(CL));

    p_si->client_list_head->next = NULL;

    return p_si;
}

int Del_Client_Node(P_CL head, P_CL del_node) // 退网使用的函数
{
    if (head == NULL)
    {
        printf("非法头节点，删除异常！\n");
        return -1;
    }
    else if (head->next == NULL)
    {
        printf("空的客户端链表，删除失败！\n");
        return -1;
    }
    else
    {
        for (P_CL befor_node = head; befor_node->next != NULL; befor_node = befor_node->next)
        {
            if (befor_node->next == del_node)
            {
                befor_node->next = del_node->next;

                del_node->next = NULL;
                free(del_node);

                break;
            }
        }
    }

    return 0;
}

void *Service_Broadcast(void *arg) // 广播发送信息
{
    P_CL client_node = (P_CL)arg;
    printf("%s连接成功!\n", client_node->ip); // 客户端连接成功
    char msg[MSG_LEN];                        // 信息的缓冲区
    char s_msg[MSG_LEN];                      // 整合信息的缓冲区
    char tem[MSG_LEN];                        // 温度的存放
    while (1)
    {
        memset(msg, 0, MSG_LEN);                              // 清空
        memset(s_msg, 0, MSG_LEN);                            // 清空
        int read_ret = read(client_node->c_fd, msg, MSG_LEN); // 查询每一个客户端的状态
        if (read_ret <= 0)
        {
            printf("%s掉线了\n", client_node->ip);
            Del_Client_Node(client_node->client_list_head, client_node); // 删除信息

            break;
        }

        sprintf(tem, "此时的温度为:%d\n", num111); // 整合发送的温度数据

        sprintf(s_msg, "用户%s:%s", client_node->ip, msg, num111); // 整个发送的用户数据

        for (P_CL tmp_node = client_node->client_list_head->next; tmp_node != NULL; tmp_node = tmp_node->next) // for循环广播
        {
            write(tmp_node->c_fd, tem, strlen(tem)); // 每个用户都能收到温度信息（只有客户端发了信息才会发）
            if (tmp_node->c_fd == client_node->c_fd) // 跳过当事人
            {
                continue;
            }

            write(tmp_node->c_fd, s_msg, strlen(s_msg)); // 发送用户的信息
        }
    }

    pthread_exit(NULL);
}

void *Wait_For_Client(void *arg) // 等待客户端连接
{
    P_SI p_si = (P_SI)arg;    // 强制类型转换
    struct sockaddr_in c_inf; // 保存连接成功的客户端信息
    int len = sizeof(c_inf);

    while (1)
    {

        memset(&c_inf, 0, sizeof(c_inf));
        int c_fd = accept(p_si->s_fd, (struct sockaddr *)&c_inf, &len); // 等待客户端连接
        if (c_fd == -1)                                                 // 连接失败
        {
            perror("accept");
            pthread_exit(NULL);
        }

        /*创建链表结点*/
        P_CL client_node = Creta_List_Node();
        if (client_node == (P_CL)-1)
        {
            printf("创建客户端结点失败!\n");
            pthread_exit(NULL);
        }

        client_node->c_fd = c_fd;

        strcpy(client_node->ip, inet_ntoa(c_inf.sin_addr)); // 获取客户端的ip地址，存进结构体

        client_node->client_list_head = p_si->client_list_head; // 存头节点

        // 头插添加进链表（头插法）
        client_node->next = p_si->client_list_head->next;
        p_si->client_list_head->next = client_node; // 更新服务器结构体指着的头指针

        // 为每一个客户端单独创建一条发送的线程
        int pthread_create_ret = pthread_create(&p_si->c_pid[p_si->client_num], NULL, Service_Broadcast, client_node);
        if (pthread_create_ret != 0)
        {
            perror("pthread_create");
            pthread_exit(NULL);
        }

        p_si->client_num++; // 更新服务器节点记录的客户端数量
    }

    pthread_exit(NULL);
}

P_CL Creta_List_Node() // 创建新节点
{
    P_CL node = (P_CL)malloc(sizeof(CL)); // 创建新节点
    if (node == NULL)                     // 创建失败
    {
        perror("malloc");
        return (P_CL)-1;
    }

    memset(node, 0, sizeof(CL)); // 清空
    node->next = NULL;           // 将指向下一位的指针为NULL

    return node; // 返回新节点
}

int Service_Working(P_SI p_si) // 服务器运行用（控制退出）
{
    int pthread_create_ret = pthread_create(&p_si->w_id, NULL, Wait_For_Client, (void *)p_si); // 创建服务器等客户端连接的线程
    if (pthread_create_ret != 0)
    {
        perror("pthread_create");
        return -1;
    }

    return 0;
}

int Service_Free(P_SI p_si) // 关闭服务器用
{
    for (P_CL tmp_node = p_si->client_list_head->next; tmp_node != NULL; tmp_node = tmp_node->next)
    {
        int write_ret = write(tmp_node->c_fd, "EXIT", 4);
        if (write_ret == -1)
        {
            perror("write");
            return -1;
        }
    }

    sleep(1);
    // 关闭套接字
    close(p_si->s_fd);

    // pthread_cancel(p_si->w_id);
    // pthread_join(p_si->w_id,NULL);

    // 第三步删除销毁链表
    free(p_si->client_list_head);

    // 第四步 free这个p_si
    free(p_si);

    return 0;
}

int init() // 初始化
{
    lcd = open("/dev/fb0", O_RDWR);
    ts = open("/dev/input/event0", O_RDONLY);
    if (lcd == -1 || ts == -1)
    {
        printf("打开文件失败！\n");
        return -1;
    }
    else
    {
        printf("打开文件成功！\n");
    }

    // 获取内存映射指针
    mmap_star = (int *)mmap(NULL, 800 * 480 * 4, PROT_READ | PROT_WRITE, MAP_SHARED, lcd, 0);
    if (mmap_star == MAP_FAILED)
    {
        printf("映射失败！\n");
        return -1;
    }
    else
    {
        printf("映射成功！\n");
    }

    p_si = Service_Init(); // 服务器初始化
    if (p_si == (P_SI)-1)
    {
        printf("服务器初始化失败！\n");
        return -1;
    }
    else
    {
        printf("服务器初始化成功！\n");
    }

    pthread_t rand_pid;

    int ret_2 = pthread_create(&rand_pid, NULL, random_num, NULL);
    if (ret_2 != 0)
    {
        perror("pthread_create");
        return -1;
    }

    return 0;
}

int show_bmp(char *bmp_path) // 照片显示
{
    int bmp = open(bmp_path, O_RDONLY);
    if (bmp == -1)
    {
        printf("打开图片失败！\n");
        return -1;
    }
    else
    {
        printf("打开图片成功！\n");
    }

    lseek(bmp, 54, SEEK_SET);

    char rgb[800 * 480 * 3];
    int read_ret = read(bmp, rgb, 800 * 480 * 3);
    if (read_ret == -1)
    {
        printf("读取像素点失败！\n");
        return -1;
    }
    else
    {
        printf("读取像素点成功！\n");
    }

    int x, y;
    for (y = 0; y < 480; y++)
    {
        for (x = 0; x < 800; x++)
        {
            *(mmap_star + 800 * (479 - y) + x) = (rgb[3 * (800 * y + x)] << 0) + (rgb[3 * (800 * y + x) + 1] << 8) + (rgb[3 * (800 * y + x) + 2] << 16);
        }
    }

    close(bmp);
    return 0;
}

int touch_xy() // 触摸坐标读取
{
    read(ts, &touch, sizeof(touch));
    if (touch.type == EV_ABS && touch.code == ABS_X)
        touch_x = touch.value * 800 / 1024; // 获取x坐标
    if (touch.type == EV_ABS && touch.code == ABS_Y)
        touch_y = touch.value * 480 / 600; // 获取x坐标
    return 0;
}

int get_slide() // 滑动
{
    int touch_flag = -1;
    int old_x_touch;
    int old_y_touch;
    while (1)
    {
        touch_xy();
        if (touch.type == EV_KEY && touch.code == BTN_TOUCH && touch.value > 0)
        {
            old_x_touch = touch_x;
            old_y_touch = touch_y;
        }
        if (touch.type == EV_KEY && touch.code == BTN_TOUCH && touch.value == 0)
        {
            touch_flag = 0;
            if (old_x_touch - touch_x > 200)
            {
                printf("向左滑动\n");
                touch_flag = 1;
            }
            if (touch_x - old_x_touch > 200)
            {
                printf("向右滑动\n");
                touch_flag = 2;
            }
            if (old_y_touch - touch_y > 200)
            {
                printf("向上滑动\n");
                touch_flag = 3;
            }
            if (touch_y - old_y_touch > 200)
            {
                printf("向下滑动\n");
                touch_flag = 4;
            }
            return touch_flag;
        }
    }
}

int free_init() // 结束退出
{
    close(lcd);
    close(ts);
    munmap(mmap_star, 800 * 480 * 4);

    return 0;
}

int mplayer_Video() // 视频
{

    int movie = 1;
    int flag = 1;
    int flagmovie = 0;
    int Video_touch_flag = 0;
    char memset_bmp[800 * 480 * 4] = {0x00};

    show_bmp("/mdata/movie.bmp");

    while (1)
    {
        Video_touch_flag = get_slide();

        if (flag == 1) // 视频界面状态
        {
            system("killall -9 mplayer");
            sleep(1);
            show_bmp("/mdata/movie.bmp");
            if (touch_x > 4 && touch_x < 59 && touch_y > 4 && touch_y < 55)
            {
                printf("退出\n");
                flag = 0;
            }
            else if (touch_x > 167 && touch_x < 323 && touch_y > 337 && touch_y < 423)
            {
                printf("播放1.avi！\n");
                flag = 2;

                /*system() 函数用于在 C 中执行 shell 命令
                mplayer 命令用于播放音频文件
                -quiet 选项用于静默播放
                -slave 选项用于交互式播放
                -geometry 0:0 选项用于指定播放器窗口的大小
                & 表示在后台运行
                */

                system("nohup mplayer -quiet -slave -geometry 0:0 /mdata/1.avi &");
                movie = 1;
            }
            else if (touch_x > 326 && touch_x < 473 && touch_y > 336 && touch_y < 417)
            {
                printf("播放2.avi！\n");
                flag = 2;
                system("nohup mplayer -quiet -slave -geometry 0:0 /mdata/2.mp4 &");
                movie = 2;
            }
            else if (touch_x > 482 && touch_x < 631 && touch_y > 338 && touch_y < 415)
            {
                printf("播放3.avi！\n");
                flag = 2;
                system("nohup mplayer -quiet -slave -geometry 0:0 /mdata/3.mp4 &");
                movie = 3;
            }

        }                   // flag 5
        else if (flag == 2) // 视频播放界面
        {
            if (touch_x > 4 && touch_x < 59 && touch_y > 4 && touch_y < 55)
            {
                printf("退出！\n");
                system("killall -9 mplayer");
                sleep(1);
                show_bmp("/mdata/movie.bmp");
                sleep(1);
                flag = 1;
                flagmovie = 0;
            }
            else
            {

                if (Video_touch_flag == 2 || Video_touch_flag == 3) // 下一个视频
                {
                    printf("下一个视频！\n");
                    if (movie > 0 && movie < 3)
                    {
                        system("killall -9 mplayer");
                        sleep(1);
                        movie++;
                        if (movie == 1)
                        {
                            system("mplayer /mdata/1.avi &");
                            flagmovie = 1;
                        }
                        else if (movie == 2)
                        {
                            system("mplayer /mdata/2.mp4 &");
                            flagmovie = 1;
                        }
                        else if (movie == 3)
                        {
                            system("mplayer /mdata/3.mp4 &");
                            flagmovie = 1;
                        }
                    }
                }
                else if (Video_touch_flag == 1 || Video_touch_flag == 4) // 播放上一个视频
                {
                    printf("上一个视频！\n");
                    if (movie > 1 && movie < 4)
                    {
                        system("killall -9 mplayer");
                        sleep(1);
                        movie--;
                        if (movie == 1)
                        {
                            system("mplayer /mdata/1.avi &");
                            flagmovie = 1;
                        }
                        else if (movie == 2)
                        {
                            system("mplayer /mdata/2.mp4 &");
                            flagmovie = 1;
                        }
                        else if (movie == 3)
                        {
                            system("mplayer /mdata/3.mp4 &");
                            flagmovie = 1;
                        }
                    }
                }
                else if (Video_touch_flag == 0 && touch_y > 55)
                {
                    printf("播放视频！\n");
                    if (flagmovie == 0) // 一开始没播放视频的，从头开始播放视频
                    {
                        if (movie == 1)
                        {
                            system("mplayer /mdata/1.avi &");
                            flagmovie = 1;
                        }
                        else if (movie == 2)
                        {
                            system("mplayer /mdata/2.mp4 &");
                            flagmovie = 1;
                        }
                        else if (movie == 3)
                        {
                            system("mplayer /mdata/3.mp4 &");
                            flagmovie = 1;
                        }
                    }
                    else if (flagmovie == 1) // 播放中，暂停
                    {
                        printf("暂停视频！\n");
                        system("killall -STOP mplayer &");
                        flagmovie = 3;
                    }
                    else if (flagmovie = 3) // 从暂停中恢复
                    {
                        printf("恢复视频！\n");
                        system("killall -CONT mplayer &");
                        flagmovie = 1;
                    }
                }
            }
        }
        else if (flag == 0)
        {
            break;
        }
    }
    return 0;
}

int madplay_Music() // 音乐
{

    int music = 1;
    int flagmusic = 0;

    show_bmp("/mdata/music.bmp"); // 显示音乐界面

    while (1)
    {
        touch_xy(); // get触摸坐标
        if (touch.type == EV_KEY && touch.code == BTN_TOUCH && touch.value == 0)
        {
            if (touch_x > 446 && touch_x < 506 && touch_y > 398 && touch_y < 455) // 下一首
            {
                printf("下一首\n");
                if (music > 0 && music < 3)
                {                                 // system() 函数用于在 C 中执行 shell 命令
                    system("killall -9 madplay"); // 杀死所有名为 "madplay" 的进程 -9强制杀死进程
                    music++;
                    if (music == 1)
                    {
                        system("madplay /mdata/1.mp3 &"); //& 表示在后台运行
                        flagmusic = 1;
                    }
                    else if (music == 2)
                    {
                        system("madplay /mdata/2.mp3 &");
                        flagmusic = 1;
                    }
                    else if (music == 3)
                    {
                        system("madplay /mdata/3.mp3 &");
                        flagmusic = 1;
                    }
                }
            }
            else if (touch_x > 352 && touch_x < 421 && touch_y > 393 && touch_y < 463) // 暂停和播放功能
            {
                printf("暂停和播放\n");
                if (flagmusic == 0) // 一开始没播放音乐的，从头开始播放音乐
                {
                    printf("播放\n");
                    if (music == 1)
                    {
                        system("madplay /mdata/1.mp3 &");
                        flagmusic = 1;
                    }
                    else if (music == 2)
                    {
                        system("madplay /mdata/2.mp3 &");
                        flagmusic = 1;
                    }
                    else if (music == 3)
                    {
                        system("madplay /mdata/3.mp3 &");
                        flagmusic = 1;
                    }
                }
                else if (flagmusic == 1) // 播放中，暂停
                {
                    printf("暂停\n");
                    system("killall -STOP madplay &"); // 用于停止进程
                    flagmusic = 3;
                }
                else if (flagmusic = 3) // 从暂停中恢复
                {
                    printf("恢复\n");
                    system("killall -CONT madplay &"); // 用于恢复进程
                    flagmusic = 1;
                }
            }
            else if (touch_x > 271 && touch_x < 321 && touch_y > 400 && touch_y < 453) // 上一首
            {
                printf("上一首\n");
                if (music > 1 && music < 4)
                {
                    system("killall -9 madplay");
                    music--;
                    if (music == 1)
                    {
                        system("madplay /mdata/1.mp3 &");
                        flagmusic = 1;
                    }
                    else if (music == 2)
                    {
                        system("madplay /mdata/2.mp3 &");
                        flagmusic = 1;
                    }
                    else if (music == 3)
                    {
                        system("madplay /mdata/3.mp3 &");
                        flagmusic = 1;
                    }
                }
            }
            if (touch_x > 9 && touch_x < 90 && touch_y > 9 && touch_y < 75) // 退出
            {
                system("killall -9 madplay");

                printf("退出！\n");
                break;
            }
        }
    }

    return 0;
}

int mxm_pic() // 相册
{
    char *path[5];
    path[0] = "/mdata/1.bmp";
    path[1] = "/mdata/2.bmp";
    path[2] = "/mdata/3.bmp";
    path[3] = "/mdata/4.bmp";
    path[4] = "/mdata/5.bmp";

    int status = 0; // 状态量，0为在相册页面，1为放大图片
    int n = 0;

    printf("进入相册模式");
    while (1)
    {
        if (status == 0) // 在主页面
        {
            show_bmp("/mdata/face.bmp");
            get_slide();
            if (touch_x < 240 && touch_y < 220 && touch_y > 75) // 第一张图片位置
            {
                printf("第一张！\n");
                show_bmp(path[0]);
                n = 0;
                status = 1;
            }
            if (touch_x > 240 && touch_x < 480 && touch_y < 220 && touch_y > 75) // 第二张图片位置
            {
                printf("第二张！\n");
                show_bmp(path[1]);
                n = 1;
                status = 1;
            }
            if (touch_x > 480 && touch_x < 720 && touch_y > 75 && touch_y < 220) // 第三张图片位置
            {
                printf("第三张！\n");
                show_bmp(path[2]);
                n = 2;
                status = 1;
            }
            if (touch_x < 240 && touch_y > 220 && touch_y < 364) // 第四张图片位置
            {
                printf("第四张！\n");
                show_bmp(path[3]);
                n = 3;
                status = 1;
            }
            if (touch_x > 240 && touch_x < 480 && touch_y > 220 && touch_y < 364) // 第五张图片位置
            {
                printf("第五张！\n");
                show_bmp(path[4]);
                n = 4;
                status = 1;
            }
            if (touch_x > 720 && touch_x < 800 && touch_y > 80 && touch_y < 160) // 返回主页
            {
                printf("返回主页\n");
                show_bmp("/mdata/zhuomian_g.bmp");
                // status = 0;
                break;
                return -1;
            }
        }

        if (status == 1) // 相册内部
        {
            int slide_get = get_slide();                                                                              // 获取滑动值
            if ((touch_x > 0 && touch_x < 200 && touch_y > 280 && touch_y < 480) || slide_get == 2 || slide_get == 4) // 上一张
            {
                printf("上一张！\n");
                if (n == 0)
                {
                    n = 4;
                }
                else
                {
                    n--;
                }
                show_bmp(path[n]);
            }
            if ((touch_x > 600 && touch_x < 800 && touch_y > 280 && touch_y < 480) || slide_get == 1 || slide_get == 3) // 下一张
            {
                printf("下一张！\n");
                if (n == 4)
                {
                    n = 0;
                }
                else
                {
                    n++;
                }
                show_bmp(path[n]);
            }
            if (touch_x > 700 && touch_x < 800 && touch_y > 0 && touch_y < 100) // 右上角
            {
                printf("返回主相册\n");
                show_bmp("/mdata/face.bmp");
                status = 0;
                // break;
            }
        }
    }
    return 0;
}

int Game() // 刮刮乐
{
    int fd = 0;
    int bmp_fd = 0;
    bmp_fd = open("/mdata/caipiao.bmp", O_RDWR);
    if (bmp_fd == -1)
    {
        perror("open func.bmp fail");
    }

    lseek(bmp_fd, 54, SEEK_SET);
    unsigned char bmp[800 * 480 * 4] = {0};
    read(bmp_fd, bmp, sizeof(bmp));
    unsigned int temp[800 * 480] = {0};

    int i = 0;
    int j = 0;

    // rgb转为bgr
    for (i = 0; i < 800 * 480; i++)
    {
        temp[i] = bmp[3 * i] | bmp[3 * i + 1] << 8 | bmp[3 * i + 2] << 16;
    }
    // 上下翻转
    for (i = 0; i < 480; i++)
    {
        for (j = 0; j < 800; j++)
        {
            mmap_star[(480 - 1 - i) * 800 + j] = temp[i * 800 + j];
        }
    }
    int bmp_w, bmp_h;
    lseek(bmp_fd, 18, SEEK_SET);
    read(bmp_fd, &bmp_w, 4);
    read(bmp_fd, &bmp_h, 4);
    printf("图片的长：%d---宽：%d\n", bmp_w, bmp_h);

    int flag = 1;
    int x, y;
    int fh;
    struct input_event ts_buf;
    if (flag)
    {
        // 先将要刮的地方遮住
        for (int i = 120; i < 420; i++)
        {
            for (int j = 30; j < 750; j++)
            {
                mmap_star[800 * (i) + j] = 0x111111;
            }
        }
        flag = 0;
        // printf("遮住了");
    }
    while (1)
    {

        fh = read(ts, &ts_buf, sizeof(ts_buf));

        touch_xy();

        srand((int)time(0));

        if (touch_x < 50 && touch_y < 50) // 退出
        {
            printf("退出");
            return -1;
        }

        for (int i = touch_y; i < bmp_h && i < 50 + touch_y; i += rand() % 10)
        {
            for (int j = touch_x; j < bmp_w && j < 50 + touch_x; j += rand() % 10)
            {
                mmap_star[800 * (i) + j] = temp[bmp_w * i + j];
                // 刷多一组，不然刷出来的效果不好
                if (i + 1 < 480 && j + 1 < 800)
                {
                    mmap_star[800 * (i + 1) + j + 1] = temp[bmp_w * (i + 1) + (j + 1)];
                }
            }
            //}
        }
    }
    close(bmp_fd);
}

int mqtt()
{
    int t = 0;
    int slide_flag=0;
    pthread_t send_pid;
    int ret_1 = pthread_create(&send_pid, NULL, mqtt_Send_Msg, NULL);
    if (ret_1 != 0)
    {
        perror("pthread_create");
        return -1;
    }
    while (1)
    {
        slide_flag=get_slide();
        if (touch_x > 0 && touch_x < 100 && touch_y > 0 && touch_y < 100)
        {
            return -1;
        }

        if(slide_flag ==1)
        {

            mqtts_flag = 1;

        }

        if(slide_flag ==2)
        {

            mqtts_flag = 0;

        }

        if (mqtts_flag == 1)
        {
            show_bmp("/mdata/on.bmp");
        }
        else if(mqtts_flag == 0)
        {
            show_bmp("/mdata/off.bmp");
        }
    }
}

int guangbo()
{
    // 创建一条线程去等待客户端的连接，在Service_Working(p_si)中有退出的出口
    show_bmp("/mdata/broadcast.bmp");

    int service_woring_ret = Service_Working(p_si);
    if (service_woring_ret == -1)
    {
        printf("服务器运行失败！\n");
        return -1;
    }

    // char mask[5];

    while (1)
    {
        // memset(mask,0,5);

        touch_xy();

        if (touch_x > 0 && touch_x < 100 && touch_y > 0 && touch_y < 100)
        {
            // 服务器结束，退出
            Service_Free(p_si);
            printf("退出guangbo");
            return -1;
        }
        /*
        //这里是原来的在烧录软件控制退出，可以改成点击返回后回到桌面
        printf("可输入EXIT退出服务端！\n");
        //scanf("%s",mask);

        if(strcmp(mask,"EXIT") == 0)
        {
            printf("服务器即将退出！\n");
            sleep(2);
            //服务器结束，退出
            Service_Free(p_si);
            printf("退出guangbo");
            return -1;
            break;
        }*/
    }
}

void *random_num(void *arg)
{
    srand(time(NULL));

    while (1)
    {
        num111 = rand() % 10 + 20; // 产生随机数
    }

    pthread_exit(NULL);
}

/*****************************************************************************/
/* Local Definitions ( Constant and Macro )                                  */
/*****************************************************************************/
/** 心跳时间*/
#define CN_LIFE_TIME_MQTTS 120

/** OneNET平台产品ID*/
#define CN_PRODUCT_ID_MQTTS "8M3SdmQtzK"

/** 设备唯一识别码，产品内唯一*/
#define CN_DEVICE_NAME_MQTTS "zigbee"

/** 认证信息--设备key*/
#define CN_ACCESS_KEY_MQTTS "ZldldzV6akxpbnJoTE16em5XNGhZVUpQS0ZiNktnTzY="

/*****************************************************************************/
/* Structures, Enum and Typedefs                                             */
/*****************************************************************************/

/*****************************************************************************/
/* Local Function Prototype                                                  */
/*****************************************************************************/
int32_t iot_mqtts_cb_func(enum iot_dev_event_e event, void *data, uint32_t data_len);

/*****************************************************************************/
/* Local Variables                                                           */
/*****************************************************************************/
struct iot_data_s data_notify_list[] = {{.data_id = (const uint8_t *)"data_stream_demo", .data_type = IOT_DATA_TYPE_INT, .buf.val_int = 0}};
int32_t data_notify_num = sizeof(data_notify_list) / sizeof(struct iot_data_s);
/*****************************************************************************/
/* Global Variables                                                          */
/*****************************************************************************/

/*****************************************************************************/
/* Function Implementation                                                   */
/*****************************************************************************/
int32_t iot_mqtts_cb_func(enum iot_dev_event_e event, void *data, uint32_t data_len)
{
    struct iot_dev_cmd_t *cmd_data_ptr = (struct iot_dev_cmd_t *)data;

    switch (event)
    {
    case IOT_DEV_CMD_RECEIVED:
    {
        /** 这里回环回去，测试下发命令是否成功*/
        osl_memcpy(cmd_data_ptr->resp, cmd_data_ptr->data, cmd_data_ptr->data_len);
        cmd_data_ptr->resp_len = cmd_data_ptr->data_len;
        logd("mqtts cmd arrived!");
        break;
    }
    case IOT_DEV_RESOURCE_PUSHED:
    {
        logd("mqtts other topic cmd received!");
        break;
    }
    default:
        break;
    }
    return 0;
}
#ifdef USE_HAL_DRIVER
extern int HAL_main(void);
#endif
#ifdef USE_STDPERIPH_DRIVER
extern int stdperiph_main(void);
#endif

void *mqtt_Send_Msg(void *arg)
{
    void *iot_dev_ptr = NULL;

    int32_t loop;
    handle_t timer_handle = 0;
    uint32_t timer_period = 2;
    struct iot_cloud_config_t mqtts_config = {
        .product_id = (uint8_t *)CN_PRODUCT_ID_MQTTS,
        .device_sn = (uint8_t *)CN_DEVICE_NAME_MQTTS,
        .auth_code = (uint8_t *)CN_ACCESS_KEY_MQTTS,
        .life_time = CN_LIFE_TIME_MQTTS,
    };

#ifdef USE_HAL_DRIVER
    HAL_main();
#endif
#ifdef USE_STDPERIPH_DRIVER
    stdperiph_main();
#endif

    /** 打开设备*/
    iot_dev_ptr = iot_dev_open(IOT_PROTO_MQTTS, &mqtts_config, &iot_mqtts_cb_func, 60000);

    if (iot_dev_ptr != NULL)
    {
        logd("mqtts iot dev open sucessed!");
    }
    else
    {
        logd("mqtts iot dev open failed");
        goto exit;
    }

    /** 设备处理主循环*/
    while (1)
    {

        /** 循环处理，包括心跳*/
        if (0 != iot_dev_step(iot_dev_ptr, 2000))
        {
            break;
        }

        /** 定时上报数据示例*/
        if (timer_handle == 0)
        {
            timer_handle = countdown_start(timer_period * 1000);
        }
        else if (countdown_is_expired(timer_handle) == 1)
        {
            if (mqtts_flag == 1)
            {
                /** 上报数据到OneNET*/
                for (loop = 0; loop < data_notify_num; loop++)
                {
                    data_notify_list[loop].buf.val_int = num111;
                    if (0 == iot_dev_notify(iot_dev_ptr, &data_notify_list[loop], 10000))
                    {
                        logd("mqtts notify ok");
                    }
                }
            }
            countdown_set(timer_handle, timer_period * 1000);
        }

        time_delay_ms(50);
    }

exit:
    /** 关闭设备*/
    iot_dev_close(iot_dev_ptr, 3000);
    logd("mqtts test device closed!");

    return 0;
}

int main()
{
    int code[] = {6, 1, 2};
    int mycode[] = {0, 0, 0};
    int n = 0, m = 0, i = 0;
    int flag = 0;

    init();
    /*
    system("nohup mplayer -quiet -slave -geometry 0:0 /mdata/kaiji.mp4 &");
    sleep(15);
    system("killall mplayer");
    sleep(1);
    */
    show_bmp("/mdata/bmpcode_g.bmp");

    while (1)
    {
        get_slide();

        if (flag == 0) // 锁屏界面
        {
            // 密码显示
            if (touch_x > 242 && touch_x < 347 && touch_y > 195 && touch_y < 262) // 1
            {
                mycode[n] = 1;
                n++;
                m++;
                switch (m)
                {
                case 1:
                    show_bmp("/mdata/bmpcode_1.bmp");
                    break;
                case 2:
                    show_bmp("/mdata/bmpcode_2.bmp");
                    break;
                case 3:
                    show_bmp("/mdata/bmpcode_3.bmp");
                    break;
                }
                printf("1\n");
            }
            else if (touch_x > 347 && touch_x < 452 && touch_y > 195 && touch_y < 262) // 2
            {
                mycode[n] = 2;
                n++;
                m++;
                switch (m)
                {
                case 1:
                    show_bmp("/mdata/bmpcode_1.bmp");
                    break;
                case 2:
                    show_bmp("/mdata/bmpcode_2.bmp");
                    break;
                case 3:
                    show_bmp("/mdata/bmpcode_3.bmp");
                    break;
                }
                printf("2\n");
            }
            else if (touch_x > 452 && touch_x < 557 && touch_y > 195 && touch_y < 262) // 3
            {
                mycode[n] = 3;
                n++;
                m++;
                switch (m)
                {
                case 1:
                    show_bmp("/mdata/bmpcode_1.bmp");
                    break;
                case 2:
                    show_bmp("/mdata/bmpcode_2.bmp");
                    break;
                case 3:
                    show_bmp("/mdata/bmpcode_3.bmp");
                    break;
                }
                printf("3\n");
            }
            else if (touch_x > 242 && touch_x < 347 && touch_y > 262 && touch_y < 328) // 4
            {
                mycode[n] = 4;
                n++;
                m++;
                switch (m)
                {
                case 1:
                    show_bmp("/mdata/bmpcode_1.bmp");
                    break;
                case 2:
                    show_bmp("/mdata/bmpcode_2.bmp");
                    break;
                case 3:
                    show_bmp("/mdata/bmpcode_3.bmp");
                    break;
                }
                printf("4\n");
            }
            else if (touch_x > 347 && touch_x < 452 && touch_y > 262 && touch_y < 328) // 5
            {
                mycode[n] = 5;
                n++;
                m++;
                switch (m)
                {
                case 1:
                    show_bmp("/mdata/bmpcode_1.bmp");
                    break;
                case 2:
                    show_bmp("/mdata/bmpcode_2.bmp");
                    break;
                case 3:
                    show_bmp("/mdata/bmpcode_3.bmp");
                    break;
                }
                printf("5\n");
            }
            else if (touch_x > 452 && touch_x < 557 && touch_y > 262 && touch_y < 328) // 6
            {
                mycode[n] = 6;
                n++;
                m++;
                switch (m)
                {
                case 1:
                    show_bmp("/mdata/bmpcode_1.bmp");
                    break;
                case 2:
                    show_bmp("/mdata/bmpcode_2.bmp");
                    break;
                case 3:
                    show_bmp("/mdata/bmpcode_3.bmp");
                    break;
                }
                printf("6\n");
            }
            else if (touch_x > 242 && touch_x < 347 && touch_y > 328 && touch_y < 394) // 7
            {
                mycode[n] = 7;
                n++;
                m++;
                switch (m)
                {
                case 1:
                    show_bmp("/mdata/bmpcode_1.bmp");
                    break;
                case 2:
                    show_bmp("/mdata/bmpcode_2.bmp");
                    break;
                case 3:
                    show_bmp("/mdata/bmpcode_3.bmp");
                    break;
                }
                printf("7\n");
            }
            else if (touch_x > 347 && touch_x < 452 && touch_y > 328 && touch_y < 394) // 8
            {
                mycode[n] = 8;
                n++;
                m++;
                switch (m)
                {
                case 1:
                    show_bmp("/mdata/bmpcode_1.bmp");
                    break;
                case 2:
                    show_bmp("/mdata/bmpcode_2.bmp");
                    break;
                case 3:
                    show_bmp("/mdata/bmpcode_3.bmp");
                    break;
                }
                printf("8\n");
            }
            else if (touch_x > 452 && touch_x < 557 && touch_y > 328 && touch_y < 394) // 9
            {
                mycode[n] = 9;
                n++;
                m++;
                switch (m)
                {
                case 1:
                    show_bmp("/mdata/bmpcode_1.bmp");
                    break;
                case 2:
                    show_bmp("/mdata/bmpcode_2.bmp");
                    break;
                case 3:
                    show_bmp("/mdata/bmpcode_3.bmp");
                    break;
                }
                printf("9\n");
            }
            else if (touch_x > 358 && touch_x < 466 && touch_y > 410 && touch_y < 490) // 0
            {
                mycode[n] = 0;
                n++;
                m++;
                switch (m)
                {
                case 1:
                    show_bmp("/mdata/bmpcode_1.bmp");
                    break;
                case 2:
                    show_bmp("/mdata/bmpcode_2.bmp");
                    break;
                case 3:
                    show_bmp("/mdata/bmpcode_3.bmp");
                    break;
                }
                printf("0\n");
            }

            if (n == 3)
            {
                for (i = 0; i < 3; i++)
                {
                    if (code[i] != mycode[i])
                    {
                        show_bmp("/mdata/bmpcode_c.bmp");
                        sleep(1);
                        show_bmp("/mdata/bmpcode_g.bmp");
                        for (i = 0; i < 3; i++)
                        {
                            mycode[i] = 0;
                        }
                        n = 0;
                        m = 0;
                    }
                    else // 解锁成功
                    {
                        show_bmp("/mdata/zhuomian_g.bmp");
                        flag = -1;
                    }
                }
            }
        }

        if (flag == -1) // 进入桌面
        {
            show_bmp("/mdata/zhuomian_g.bmp");
            if (touch_x > 552 && touch_x < 652 && touch_y > 92 && touch_y < 192) // 点击app 电子相册
            {

                flag = 2; // 电子相册状态
            }

            else if (touch_x > 350 && touch_x < 450 && touch_y > 92 && touch_y < 192) // 点击app 音乐播放
            {

                flag = 4; // 音乐界面状态
            }

            else if (touch_x > 148 && touch_x < 248 && touch_y > 92 && touch_y < 192) // 点击app  视频播放
            {

                flag = 5; // 视频播放状态
            }
            else if (touch_x > 552 && touch_x < 652 && touch_y > 288 && touch_y < 388) // 点击app	玩游戏
            {

                flag = 6; // 游戏状态
            }
            else if (touch_x > 148 && touch_x < 248 && touch_y > 288 && touch_y < 388) // 上传采集
            {

                flag = 7; // 采集状态
            }
            else if (touch_x > 350 && touch_x < 450 && touch_y > 288 && touch_y < 388) // 广播
            {

                flag = 8; // 广播状态
            }
        }

        if (flag == 2) // 电子相册
        {

            flag = mxm_pic();

        } // flag 3

        if (flag == 4) // 音乐界面状态
        {
            madplay_Music();
            flag = -1;
        } // flag4

        if (flag == 5) // 视频界面状态
        {
            mplayer_Video();
            flag = -1;
        } // flag 5

        if (flag == 6) // 游戏界面状态
        {
            flag = Game();
        } // flag 6

        if (flag == 7) // 上传采集
        {
            flag = mqtt();
        }

        if (flag == 8) // 广播
        {
            flag = guangbo();
        }

        printf("%d\n", flag);
        printf("while begin again\n");

    } // while循环

    free_init();
    return 0;
}
