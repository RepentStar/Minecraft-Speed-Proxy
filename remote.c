#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>
#include "log.h"
#include "websocket.h"
#include "RemoteClient.h"

extern char *remoteServerAddress;
extern int LocalPort;
extern int Remote_Port;
extern pthread_key_t Thread_Key;

void *SendingThread(void *pack);
SendingPack_t InitSending(int target_sock, int maxdata);
DataLink_t *UpSendingData(SendingPack_t *pack, DataLink_t *link, void *data, size_t size);

void *DealRemote(void *InputArg);
void *DealRemote(void *InputArg)
{
    //设置线程独享资源
    jmp_buf jmp;
    pthread_setspecific(Thread_Key, &jmp);
    //接收多线程传参
    Rcpack *pack = ((Rcpack *)InputArg);
    WS_Connection_t server = pack->server;
    WS_Connection_t client = pack->client;
    switch (setjmp(jmp))
    {
    case SIGABRT:
        log_error("线程异常，已放弃");
        return NULL;
        break;
    case SIGSEGV:
        log_error("线程段错误，尝试恢复，本恢复可能造成部分资源无法完全释放");
        return NULL;
        break;
    default:
        break;
    }
    //直接进入接收循环
    register int rsnum; //收发的数据量
    //数据包
    SendingPack_t spack = InitSending(client.sock, 2000);
    pthread_t pid;
    pthread_create(&pid, NULL, SendingThread, &spack);
    void *stackdata;
    DataLink_t *temp = spack.head;
    while (1)
    {
        stackdata = ML_Malloc(&spack.pool, 8192 * 4);
        rsnum = read(server.sock, stackdata, 8192 * 4);

        if (rsnum <= 0)
        {
            ML_Free(&spack.pool, stackdata);
            pthread_mutex_unlock(&(temp->lock));
            shutdown(client.sock, SHUT_RDWR);
            shutdown(server.sock, SHUT_RDWR);
            break;
        }
        temp = UpSendingData(&spack, temp, stackdata, rsnum);
        if (temp == NULL)
        {
            //另一侧连接已断开
            ML_Free(&spack.pool, stackdata);
            shutdown(client.sock, SHUT_RDWR);
            shutdown(server.sock, SHUT_RDWR);
            break;
        }
    }
    pthread_join(pid, NULL);
    pthread_spin_destroy(&(spack.spinlock));
    if (NULL != ML_CheekMemLeak(spack.pool))
    {
        printf("内存泄露\n");
    }

    return NULL;
}
