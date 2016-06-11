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

#define TEMPERATURE    "/sys/class/hwmon/hwmon0/temp1_input"

char *tzargentina = "America/Buenos_Aires";
char *tzutc = "UTC";
char *tzberlin = "Europe/Berlin";

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

int
main(void)
{
	char *status;
	char *avgs;
	char *tmbln;
	long temperature = 0;

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	for (;;sleep(10)) {
		avgs = loadavg();
		tmbln = mktimes("%a %Y-%b-%d %H:%M:%S (%z)", tzberlin);

		temperature = readTemperature ();

		status = smprintf("%d C L:%s %s",
				temperature,
				avgs, tmbln);
		setstatus(status);
		free(avgs);
		free(tmbln);
		free(status);
	}

	XCloseDisplay(dpy);

	return 0;
}

