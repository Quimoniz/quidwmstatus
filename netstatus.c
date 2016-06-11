#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
/* required for function invocation isspace */
#include <ctype.h>
/* Not in debug mode today
 * #define DEBUG
 */


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
	int parsingStatus;
	int curLineLength;
	int tokenCount;
	int lineNumber;
	char * curLineBuf;
	char * curTokenStr;
	char * bytesTransferedStr;
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



char * getNetstatus (struct transfer_datum * transferStats, struct parsing_var parse_vars, char * readBuf)
{
    int i = 0;
    FILE *fp = NULL;
    long charsRead = 0;
    parse_vars.lineNumber = 0;
    read_stats curReadStats;
    int prevNlPos = 0, curNlPos = 0;
    char * returnValue = malloc ( 100);


    if (   0 == access( NETWORK , R_OK) )
    {
        fp = fopen(NETWORK, "r");
	parse_vars.lineNumber = 0;

	//parse through network stats
	//Note that the following code relies on that the fread call will provide all data within one call to fread, for a line to be parsed at all, there needs to be a newline in the current buffer.
	//If one were to take a reasonable assumption, that a call to fread will not always return all the data at once, this code is very likely to FAIL.
        while ( 0 < ( charsRead = fread ( readBuf, 1, (size_t) (READ_BUF_SIZE - 1), fp) ) )
	{
            if ( 0 < charsRead )
	    {
	        readBuf[charsRead] = '\0';
	    }
	    transferStats[counterPasts].upload_bytes = 0;
	    transferStats[counterPasts].download_bytes = 0;
	    for ( i = 0; i < charsRead; i ++ )
	    {
                if ( '\n' == readBuf[i] )
		{
			prevNlPos = curNlPos;
			curNlPos  = i;
			if (1 < parse_vars.lineNumber)
			{
				parse_vars.curLineLength = curNlPos - prevNlPos;
				dumbcopy(parse_vars.curLineBuf, readBuf + prevNlPos, parse_vars.curLineLength);
				parse_vars.curLineBuf[parse_vars.curLineLength] = '\0';

                        	curReadStats = parseLine ( &parse_vars );
				if (-1 < curReadStats.download_bytes) {
#if defined DEBUG
				        printf ("adding download %+9ld to %+9ld from interface %5s\n", curReadStats.download_bytes, transferStats[counterPasts].download_bytes, curReadStats.interface);
				        printf ("adding upload   %+9ld to %+9ld from interface %5s\n", curReadStats.upload_bytes, transferStats[counterPasts].upload_bytes, curReadStats.interface);
#endif
                                        transferStats[counterPasts].download_bytes += curReadStats.download_bytes;
					transferStats[counterPasts].upload_bytes += curReadStats.upload_bytes;
				}
			}
			parse_vars.lineNumber++;
		}
	    }	    

	}
        fclose(fp);

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
  long returnVal;
  sscanf ( c, "%ld", &returnVal);
  return returnVal;
}

read_stats parseLine (struct parsing_var * parse_vars)
{
	const int DOWNLOAD_BYTES_ROW = 1;
	const int UPLOAD_BYTES_ROW = 9;
	long * byNumber = malloc(sizeof(long));
	int j = 0;
	read_stats updown_values = {-1, -1};
	parse_vars->tokenCount = 0;

	if (0 < parse_vars->curLineLength )
	{
            if (NULL != strstr(parse_vars->curLineBuf, "wlan") ||
                NULL != strstr(parse_vars->curLineBuf, "lo") ||
	        NULL != strstr(parse_vars->curLineBuf, "eth"))
	    {
	        parse_vars->parsingStatus = 0;
	        for (j = 0; j < parse_vars->curLineLength; j ++ )
		{	
                    if (0 == parse_vars->parsingStatus)
		    {
		        if ( isspace(parse_vars->curLineBuf[j]) )
			{
			    //do nothing
			} else
			{
			    parse_vars->parsingStatus = 1;
			}
		    } else
		    {
			if ( isspace(parse_vars->curLineBuf[j]))
			{
		            dumbcopy(parse_vars->curTokenStr, parse_vars->curLineBuf + (j - parse_vars->parsingStatus), parse_vars->parsingStatus);
			    parse_vars->curTokenStr[parse_vars->parsingStatus] = '\0';
			    if ( 0 == parse_vars->tokenCount )
			    {
				if ( ':' == parse_vars->curTokenStr[ parse_vars->parsingStatus -1])
				{
					parse_vars->curTokenStr[ parse_vars->parsingStatus -1 ] = '\0';
				}
				updown_values.interface = malloc (sizeof(char) * (parse_vars->parsingStatus - 1));
				dumbcopy (updown_values.interface, parse_vars->curTokenStr, parse_vars->parsingStatus);
			    } else if ( DOWNLOAD_BYTES_ROW == parse_vars->tokenCount || 
					UPLOAD_BYTES_ROW == parse_vars->tokenCount)
			    {
			        dumbcopy(parse_vars->bytesTransferedStr, parse_vars->curTokenStr, parse_vars->parsingStatus);
				parse_vars->bytesTransferedStr[parse_vars->parsingStatus] = '\n';
				parse_vars->bytesTransferedStr[parse_vars->parsingStatus + 1] = '\0';
                                sscanf(parse_vars->bytesTransferedStr, "%ld\n", byNumber);
				if ( DOWNLOAD_BYTES_ROW == parse_vars->tokenCount )
				{
					//handle bytes that are "Receive"
					updown_values.download_bytes = (*byNumber);
				} else if (UPLOAD_BYTES_ROW == parse_vars->tokenCount)
			       	{
                                        //do something about bytes that are "Transmit" (uploaded) 
					updown_values.upload_bytes = (*byNumber);
				}
#if defined DEBUG
				printf ("Token #%2d \"%10s\" parsed as %10ld\n", parse_vars->tokenCount, parse_vars->curTokenStr, (*byNumber));
#endif
			    }
			    parse_vars->tokenCount ++;
		            parse_vars->parsingStatus = 0;
			} else
			{
				parse_vars->parsingStatus ++;
			}
		    }

		}
	    }
	}
	return updown_values;
}
