#include "cachelab.h"
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int errno;

typedef struct {
    bool help;
    bool verbose;
    size_t setNum;
    size_t lineNum;
    size_t bitNum;
    char* traceFile;
} Args_t;

typedef struct {
    char operator;
    unsigned long address;
    size_t size;
} Command;

typedef struct {
    int valid;
    int tag;
    int timeStamp;
} Cache_block;

typedef struct {
    int hits;
    int misses;
    int evictions;
} Ans;

void printHelp();
Ans readCommand(Args_t args, Cache_block** cache);
void buildCache(Args_t args, Cache_block** cache);
void process(Args_t args, Command cmd, Cache_block** cache, Ans* ans);

int main(int argc, char** argv)
{
    // init arg struct
    Args_t args;
    args.help = false;
    args.verbose = false;
    args.setNum = 0;
    args.lineNum = 0;
    args.bitNum = 0;
    args.traceFile = NULL;

    const char* optString = "hvs:E:b:t:";
    int opt = 0;
    while ((opt = getopt(argc, argv, optString)) != -1) {
        switch (opt) {
        case 'h':
            args.help = true;
            printHelp();
            return 0;
            break;
        case 'v':
            args.verbose = true;
            break;
        case 's':
            args.setNum = atoi(optarg);
            break;
        case 'E':
            args.lineNum = atoi(optarg);
            break;
        case 'b':
            args.bitNum = atoi(optarg);
            break;
        case 't':
            args.traceFile = malloc(sizeof(char) * strlen(optarg));
            strcpy(args.traceFile, optarg);
            break;
        default:
            printf("invalid command argument");
            return 0;
            break;
        }
    }

    Cache_block* cache;
    buildCache(args, &cache);
    Ans answer = readCommand(args, &cache);

    printSummary(answer.hits, answer.misses, answer.evictions);
    free(args.traceFile);
    free(cache);
    return 0;
}

void printHelp()
{
    printf("Usage: ./csim [-hv] -s <num> -E <num> -b <num> -t <file>\n");
    printf("Options:\n");
    printf("  -h         Print this help message.\n");
    printf("  -v         Optional verbose flag.\n");
    printf("  -s <num>   Number of set index bits.\n");
    printf("  -E <num>   Number of lines per set.\n");
    printf("  -b <num>   Number of block offset bits.\n");
    printf("  -t <file>  Trace file.\n\n");
    printf("Examples:\n");
    printf("  linux>  ./csim -s 4 -E 1 -b 4 -t traces/yi.trace\n");
    printf("  linux>  ./csim -v -s 8 -E 2 -b 4 -t traces/yi.trace");
}

Ans readCommand(Args_t args, Cache_block** cache)
{
    FILE* fp = fopen(args.traceFile, "r");
    if (fp == NULL) {
        printf("error: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    Command cmd;
    Ans ret;
    ret.hits = 0;
    ret.misses = 0;
    ret.evictions = 0;

    while (fscanf(fp, " %c %lx,%lu\n", &cmd.operator, & cmd.address, & cmd.size) != EOF) {
        switch (cmd.operator) {
        case 'L': // load
            if (args.verbose) {
                printf("%c %lx,%lu", cmd.operator, cmd.address, cmd.size);
            }
            process(args, cmd, cache, &ret);
            if (args.verbose)
                printf("\n");
            break;

        case 'M': // modify
            if (args.verbose) {
                printf("%c %lx,%lu", cmd.operator, cmd.address, cmd.size);
            }
            process(args, cmd, cache, &ret);
            process(args, cmd, cache, &ret);
            if (args.verbose)
                printf("\n");
            break;

        case 'S': // store
            if (args.verbose) {
                printf("%c %lx,%lu", cmd.operator, cmd.address, cmd.size);
            }
            process(args, cmd, cache, &ret);
            if (args.verbose)
                printf("\n");
            break;
        case 'I':
            break;
        default:
            printf("invalid operation");
            exit(EXIT_FAILURE);
            break;
        }
        // update time stamp
        size_t cacheSize = (1 << args.setNum) * args.lineNum;
        for (size_t i = 0; i < cacheSize; i++) {
            if ((*cache)[i].valid == 1) {
                (*cache)[i].timeStamp++;
            }
        }
    }

    fclose(fp);
    return ret;
}

void buildCache(Args_t args, Cache_block** cache)
{
    size_t s = 1 << args.setNum;
    size_t e = args.lineNum;
    *cache = malloc(sizeof(Cache_block) * s * e);
    for (size_t i = 0; i < s * e; i++) {
        (*cache)[i].valid = 0;
        (*cache)[i].tag = -1;
        (*cache)[i].timeStamp = 0;
    }
}

void process(Args_t args, Command cmd, Cache_block** cache, Ans* answer)
{
    unsigned long address = cmd.address;
    address >>= args.bitNum; // 清除 offset，因为不需要
    unsigned long mask = (-1UL) >> (64 - args.setNum);
    size_t setIndex = mask & address;
    address >>= args.setNum;
    size_t tagBits = address;

    size_t base = setIndex * args.lineNum;
    // 第一次寻找，如果找到即 hit
    for (size_t i = base; i < base + args.lineNum; i++) {
        if ((*cache)[i].valid == 1 && (*cache)[i].tag == tagBits) {
            answer->hits++;
            (*cache)[i].timeStamp = 0;
            if (args.verbose) {
                printf(" hit");
            }
            return;
        }
    }

    // 如果没有命中 开始第二次遍历，寻找是否有空的，如果有就修改
    answer->misses++;
    if (args.verbose) {
        printf(" miss");
    }
    for (size_t i = base; i < base + args.lineNum; i++) {
        if ((*cache)[i].valid == 0) {
            (*cache)[i].valid = 1;
            (*cache)[i].tag = tagBits;
            (*cache)[i].timeStamp = 0;
            return;
        }
    }

    // 如果没有找到，则是 eviction
    answer->evictions++;
    if (args.verbose) {
        printf(" eviction");
    }
    size_t lruIndex = 0;
    int maxTime = -1;
    for (size_t i = base; i < base + args.lineNum; i++) {
        if ((*cache)[i].timeStamp > maxTime) {
            lruIndex = i;
            maxTime = (*cache)[i].timeStamp;
        }
    }
    (*cache)[lruIndex].valid = 1;
    (*cache)[lruIndex].tag = tagBits;
    (*cache)[lruIndex].timeStamp = 0;
}