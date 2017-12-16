#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
/* required for function invocation isspace */
#include <ctype.h>
/* Not in debug mode today
 * #define DEBUG
 */
/* required for strtok */
#include <string.h>


#define NETWORK "/proc/net/dev"
#define BACKLOG_THIS_MANY_ITERATIONS 3

#include <string.h>

typedef struct read_stats read_stats;
struct read_stats {
  long download_bytes;
  long upload_bytes;
  char * interface;
};
struct parsing_var {
  int lineNumber;
  char * curLineBuf;
};
struct transfer_datum {
  long upload_bytes;
  long download_bytes;
};

const int COUNT_PAST = 1000;
const long READ_BUF_SIZE = 1024;
int counterPasts = 0;

char * curNetStatus (struct transfer_datum * transferStats);
char * beautifyByteCount (long byteCount);
void dumbcopy (char * src, char * dst, long bytes_to_copy);
void dumbprintLong (long argument);
read_stats parseLine (struct parsing_var * parse_vars);
long getDownloadIncrement ( struct transfer_datum * transferStats, int currentOffset, int backlogDepth );
long getUploadIncrement ( struct transfer_datum * transferStats, int currentOffset, int backlogDepth );



char * getNetstatus (struct transfer_datum * transferStats, struct parsing_var parse_vars)
{
  FILE *fp = NULL;
  long charsRead = 0;
  parse_vars.lineNumber = 0;
  read_stats curReadStats;
  char* curTokEnd = NULL;
  char* curTokStart = NULL;
  char * returnValue = malloc ( 100);
  char* mallocSwap = NULL;
  char* readBuf = malloc(READ_BUF_SIZE);

  if (   0 == access( NETWORK , R_OK) )
  {
    fp = fopen(NETWORK, "r");
    parse_vars.lineNumber = 0;
    parse_vars.curLineBuf = NULL;

    //parse through network stats
    while ( 0 < ( charsRead = fread ( readBuf, 1, (size_t) (READ_BUF_SIZE - 1), fp) ) )
    {
      readBuf[charsRead] = '\0';
      curTokStart = readBuf;
      transferStats[counterPasts].upload_bytes = 0;
      transferStats[counterPasts].download_bytes = 0;

      curTokEnd = strchr(readBuf, '\n');
      /* check if there is some overlap from a previous iteration */
      if(NULL != parse_vars.curLineBuf)
      {
        /* only do something before the loop, if a token end was found */
        if(NULL != curTokEnd)
        {
          /* allocate memory for a new string containing the overlap buf and the current token
           * copy the char sequence from the overlap buf and from the current token to it
           * finally free the old memory */
          curTokEnd[0] = '\0';
          mallocSwap = malloc(strlen(readBuf) + strlen(parse_vars.curLineBuf) + 1);
          memcpy(mallocSwap, parse_vars.curLineBuf, strlen(parse_vars.curLineBuf));
          memcpy(mallocSwap + strlen(parse_vars.curLineBuf), readBuf, curTokEnd-readBuf);
          mallocSwap[strlen(parse_vars.curLineBuf) + (curTokEnd-readBuf)] = '\0';
          free(parse_vars.curLineBuf);
          parse_vars.curLineBuf = mallocSwap;
 
          /* process line */
          if(1 < parse_vars.lineNumber)
          {
            curReadStats = parseLine(&parse_vars);
            transferStats[counterPasts].download_bytes += curReadStats.download_bytes;
            transferStats[counterPasts].upload_bytes += curReadStats.upload_bytes;
            parse_vars.curLineBuf = NULL;

          }

          /* count up the counters */
          parse_vars.lineNumber++;
          curTokStart = curTokEnd + 1;
          curTokEnd = strchr(curTokEnd + 1, '\n');
        }
      }
      /* iterate through the tokens in the current read out */
      for(; NULL != curTokEnd; curTokEnd = strchr(curTokEnd + 1, '\n')) 
      {
        /* process each found line */
        if(1 < parse_vars.lineNumber)
        {
          curTokEnd[0] = '\0'; 
          parse_vars.curLineBuf = curTokStart;
          curReadStats = parseLine(&parse_vars);
          transferStats[counterPasts].download_bytes += curReadStats.download_bytes;
          transferStats[counterPasts].upload_bytes += curReadStats.upload_bytes;
          parse_vars.curLineBuf = NULL;
        }

        /* count up the counters */
        curTokStart = curTokEnd + 1;
        parse_vars.lineNumber++;
      }

      /* process the last token */
      if((curTokStart - readBuf) < charsRead)
      {
        /* if curlineBuf is set, then the loop was not executed, the current read out contains no newline */
        if(NULL != parse_vars.curLineBuf)
        {
          mallocSwap = malloc(charsRead + strlen(parse_vars.curLineBuf) + 1);
          memcpy(mallocSwap, parse_vars.curLineBuf, strlen(parse_vars.curLineBuf));
          memcpy(mallocSwap + strlen(parse_vars.curLineBuf), readBuf, charsRead);
          mallocSwap[strlen(parse_vars.curLineBuf) + charsRead] = '\0';
          free(parse_vars.curLineBuf);
          parse_vars.curLineBuf = mallocSwap;
        /* if curline Buf is not set, put the last token to newly allocated memory */
        } else
        {
          mallocSwap = malloc(strlen(curTokStart));
          memcpy(mallocSwap, curTokStart, strlen(curTokStart));
          mallocSwap[strlen(curTokStart)] = '\0';
          parse_vars.curLineBuf = mallocSwap;
        }
      }
    }
    /* proccess last line */
    if(NULL != parse_vars.curLineBuf)
    {
      if(1 < parse_vars.lineNumber)
      {
        curReadStats = parseLine(&parse_vars);
        transferStats[counterPasts].download_bytes += curReadStats.download_bytes;
        transferStats[counterPasts].upload_bytes += curReadStats.upload_bytes;
        parse_vars.curLineBuf = NULL;
        parse_vars.lineNumber++;
      }
      free(parse_vars.curLineBuf);
    }
    fclose(fp);
    free(readBuf);

    char * netstats_string;
    netstats_string = curNetStatus (transferStats);
    returnValue = netstats_string;
  
    counterPasts = (counterPasts + 1) % COUNT_PAST;
  } else
  {
    fprintf(stderr, "Failed to get network information from \"%s\"\n", NETWORK );
  }

  return returnValue;
}

char * curNetStatus (struct transfer_datum * transferStats)
{
  char * netstats_string = malloc (32);
  char * downStr;
  char * upStr;
  int lookUpThisManyIterations = BACKLOG_THIS_MANY_ITERATIONS;
  if ( lookUpThisManyIterations > counterPasts )
  {
    lookUpThisManyIterations = counterPasts;
  }
  if ( 0 != counterPasts)
  {
    downStr = beautifyByteCount ( getDownloadIncrement (transferStats, counterPasts, lookUpThisManyIterations));
    upStr   = beautifyByteCount ( getUploadIncrement   (transferStats, counterPasts, lookUpThisManyIterations));
  } else
  {
    downStr = beautifyByteCount (transferStats[counterPasts].download_bytes);
    upStr =   beautifyByteCount (transferStats[counterPasts].upload_bytes  );
  }
  sprintf (netstats_string, "%7s | %-7s", downStr, upStr);
  free (downStr);
  free (upStr);
  return netstats_string;
}


char * beautifyByteCount (long byteCount)
{
  const char * units [8];
  units [0] = "B";
  units [1] = "KB";
  units [2] = "MB";
  units [3] = "GB";
  units [4] = "TB";
  units [5] = "PB";
  units [6] = "EB";
  units [7] = "ZB";
  long i = 0;
  int j = 0;
  long dividedByteCount = byteCount;
  //maximum length can only be 19 + 1 + 2 + 1= 22
  // (size of long + whitespace + unit-characters + terminating null byte
  //since the decimal representation of a long does not exceed 19 characters.
  //but "better be safe than sorry", so allocate an additional ~30% percent, or additional 7 bytes
        char * beautifiedStr = malloc (sizeof(char) * 30);



  for (i = 1024; i < byteCount; j++)
  {
    dividedByteCount = dividedByteCount / 1024;
    if ( 7 == j)
    {
      break;
    }
    i = i * 1024;
  }
        sprintf ( beautifiedStr, "%ld ", dividedByteCount );
        strcpy ( (char *) ( (int) beautifiedStr + strlen(beautifiedStr)), units[j]);
  return beautifiedStr;
}

long getDownloadIncrement ( struct transfer_datum * transferStats, int currentOffset, int backlogDepth )
{
  long deltaBytes = 0;
  int backlogOffset = (currentOffset % COUNT_PAST) - backlogDepth;
  
  if ( 0 > backlogOffset)
  {
    backlogOffset = (backlogOffset + COUNT_PAST) % COUNT_PAST;
  }
  deltaBytes = transferStats[ currentOffset % COUNT_PAST ].download_bytes - transferStats[ backlogOffset ].download_bytes;

  return deltaBytes;
}

long getUploadIncrement ( struct transfer_datum * transferStats, int currentOffset, int backlogDepth )
{
  long deltaBytes = 0;
  int backlogOffset = (currentOffset % COUNT_PAST) - backlogDepth;
  
  if ( 0 > backlogOffset)
  {
    backlogOffset = (backlogOffset + COUNT_PAST) % COUNT_PAST;
  }
  deltaBytes = transferStats[ currentOffset % COUNT_PAST ].upload_bytes - transferStats[ backlogOffset ].upload_bytes;

  return deltaBytes;
}



void dumbcopy (char * dst, char * src, long bytes_to_copy)
{
  long i = 0;
  for ( ; i < bytes_to_copy; i ++)
  {
    dst[i] = src [i];
  }
}

long stringAsLong (char *c) {
  long returnVal = 0;
  sscanf ( c, "%ld", &returnVal);
  return returnVal;
}

read_stats parseLine (struct parsing_var * parse_vars)
{
  const int DOWNLOAD_BYTES_ROW = 1;
  const int UPLOAD_BYTES_ROW = 9;
  read_stats updown_values = {-1, -1};

  char* curToken = NULL;
  char* nextToken = NULL;

  /* tokenize curLineBuf */
  curToken = strtok_r(parse_vars->curLineBuf, " ", &nextToken);
    
  for(int columnIndex = 0; NULL != curToken; ((++columnIndex) && (curToken = strtok_r(NULL, " ", &nextToken))))
  {
    /* assign values to struct members */
    if(0 == columnIndex)
    {
      updown_values.interface = malloc(strlen(curToken));
      memcpy(updown_values.interface, curToken, strlen(curToken));
    } else if(DOWNLOAD_BYTES_ROW == columnIndex)
    {
      updown_values.download_bytes = stringAsLong(curToken);
    } else if(UPLOAD_BYTES_ROW == columnIndex)
    {
      updown_values.upload_bytes = stringAsLong(curToken);
    }
  }

  return updown_values;
}
