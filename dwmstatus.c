#define _BSD_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <X11/Xlib.h>

//For tm.tm_gmtoff, well doesn't work :/
#define _GNU_SOURCE

#define BATT_NOW        "/sys/class/power_supply/BAT0/charge_now"
#define BATT_FULL       "/sys/class/power_supply/BAT0/charge_full"
#define BATT_STATUS       "/sys/class/power_supply/BAT0/status"
#define TEMPERATURE	"/sys/class/hwmon/hwmon0/temp1_input"
#define NETWORK "/proc/net/dev"
#define SYSTEM_TIMEZONE    "/etc/timezone"

#include <string.h>
#include <errno.h>
#include <netstatus.c>

struct battery_status {
	long maxLoad;
	long curLoad;
	char loadingState [256];
};


char *tzargentina = "America/Buenos_Aires";
char *tzberlin = "Europe/Berlin";
//Default-value and Fallback
char *tzutc = "UTC";

static Display *dpy;


char *
smprintf(char *fmt, ...)
{
	va_list fmtargs;
	char *ret;
	int len;

	va_start(fmtargs, fmt);
	len = vsnprintf(NULL, 0, fmt, fmtargs);
	va_end(fmtargs);

	ret = malloc(++len);
	if (ret == NULL) {
		perror("malloc");
		exit(1);
	}

	va_start(fmtargs, fmt);
	vsnprintf(ret, len, fmt, fmtargs);
	va_end(fmtargs);

	return ret;
}

void
settz(char *tzname)
{
	setenv("TZ", tzname, 1);
}

char *
mktimes(char *fmt, char *tzname)
{
	char buf[129];
	time_t tim;
	struct tm *timtm;

	memset(buf, 0, sizeof(buf));
	settz(tzname);
	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL) {
		perror("localtime");
		exit(1);
	}

	if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
		fprintf(stderr, "strftime == 0\n");
		exit(1);
	}

	return smprintf("%s", buf);
}


void
setstatus(char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

char *
loadavg(void)
{
	double avgs[3];

	if (getloadavg(avgs, 3) < 0) {
		perror("getloadavg");
		exit(1);
	}

	return smprintf("%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
}

char * readBatteryState (struct battery_status * batState)
{
	batState->maxLoad = 100;
	batState->curLoad = 100;
	//batState->loadingState = "Full";
	batState->loadingState[0] = 'F';
	batState->loadingState[1] = 'u';
	batState->loadingState[2] = 'l';
	batState->loadingState[3] = 'l';
	batState->loadingState[4] = '\0';
	
	FILE *fp = NULL;
	char s = '?';
	char *loadingStateTemp = malloc (256);
	int i = 0;

	if (   0 == access( BATT_NOW   , R_OK)
	&& 0 == access( BATT_FULL  , R_OK)
	&& 0 == access( BATT_STATUS, R_OK))
	{
		batState->curLoad = 1;
		batState->maxLoad = 1;

		fp = fopen(BATT_NOW, "r");
		fscanf(fp, "%ld\n", &(batState->curLoad));
		fclose(fp);
		fp = fopen(BATT_FULL, "r");
		fscanf(fp, "%ld\n", &(batState->maxLoad));
		fclose(fp);
		fp = fopen(BATT_STATUS, "r");
		fscanf(fp, "%s\n", loadingStateTemp);
		fclose(fp);
                for (i = 0; i < 256; i++ )
		{
			batState->loadingState[i] = loadingStateTemp [i];
			if ( '\0' == batState->loadingState[i] )
			{
				break;
			}
		}
		free(loadingStateTemp);



		if (strcmp(batState->loadingState,"Charging") == 0)
			s = '+';
		if (strcmp(batState->loadingState,"Discharging") == 0)
			s = '-';
		if (strcmp(batState->loadingState,"Full") == 0)
			s = '=';

		return smprintf("%c%ld%%", s,((batState->curLoad)*100/(batState->maxLoad)));
	} else {
		fprintf(stderr, "Failed to get battery state from \"%s\"\n", BATT_NOW); 
		exit(1);
	}
	return "\0";
}

long readTemperature ()
{
	long temperature = 0;
	FILE *fp = NULL;
	if ((fp = fopen(TEMPERATURE, "r"))) {
		fscanf(fp, "%ld\n", &temperature);
		fclose (fp);
		temperature = temperature / 1000;
	} else {
		fprintf(stderr, "Failed to get temperature value from \"%s\"\n", TEMPERATURE);
		exit(1);
	}
	return temperature;
}

void readSystemTimezone (char * timezone)
{
	FILE * fp;

	if ( 0 == access (SYSTEM_TIMEZONE, R_OK) )
	{
		fp = fopen (SYSTEM_TIMEZONE, "r");
		//read time zone
		fscanf (fp, "%s\n", timezone);
		fclose (fp);
	} else {
	        free(timezone);
		timezone = malloc (strlen(tzutc) + 1);
		strcpy(timezone, tzutc);
	}
}

int
main(void)
{
  char *tzSystem;
  char *status;
  char *avgs;
  char *tmbln;
  char *batteryStatus;
  batteryStatus = " ";
  long temperature = 0;

  tzSystem = malloc (50);
  struct parsing_var parse_vars = { .parsingStatus=0, .curLineLength=0, .tokenCount=0, };
  char * readBuf = malloc(sizeof(char) * READ_BUF_SIZE);
  parse_vars.curLineBuf = malloc(sizeof(char) * READ_BUF_SIZE);
  parse_vars.bytesTransferedStr = malloc(sizeof(char) * READ_BUF_SIZE);
  parse_vars.curTokenStr = malloc(sizeof(char) * READ_BUF_SIZE);
  struct battery_status * batState = malloc (sizeof(long) * 2 + sizeof(char*));
  struct transfer_datum * transferStats = malloc (sizeof(struct transfer_datum) * COUNT_PAST);

  if (!(dpy = XOpenDisplay(NULL))) {
	fprintf(stderr, "dwmstatus: cannot open display.\n");
	return 1;
  }

  readSystemTimezone(tzSystem);

  for (;;sleep(10)) {
  	avgs = loadavg();

  	tmbln = mktimes("%a %Y-%b-%d %H:%M:%S (%z)", tzberlin);
  	
  	batteryStatus = readBatteryState (batState);
  	temperature = readTemperature ();
  	char * netstats_string = getNetstatus(transferStats, parse_vars, readBuf);


  	status = smprintf("%s %d C %s L:%s %s",
  		netstats_string,
  		temperature,
  		batteryStatus,
  		avgs, tmbln);
  	setstatus(status);
  	free(netstats_string);
  	free(avgs);
  	free(tmbln);
  	free(status);
  }

  free(tzSystem);

  XCloseDisplay(dpy);

  return 0;
}




