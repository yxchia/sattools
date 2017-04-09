#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <netdb.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define PORT 7264
#define MAXPORT 7274
#define IP "127.0.0.1"
#define LIM 2048
#define NMAX 128

#define SCHEDULED 0
#define STARTED 1
#define FINISHED 2

// Observation struct contains observation time, celestial coords and camera name
struct observation {
  char stime[20],sra[15],sde[15],camname[15];
  time_t ptime;
  float dt;
};

int fgetline(FILE *file,char *s,int lim);
void send_position(char *sra,char *sde,char *camname);
time_t decode_time(char *stm);
char datadir[127];

int main(int argc, char *argv[]) 
{
  int i=0,nobs,flag=0;
  time_t rawtime,aimtime;
  struct tm *ptm,*rtm;
  char buf[20],line[LIM],stm[20],sra[15],sde[15],pra[15],pde[15];
  FILE *file;
  struct observation obs[NMAX];
  char *env;

  // Get environment variables
  env=getenv("ST_DATADIR");
  if (env!=NULL) {
    strcpy(datadir,env);
  } else {
    printf("ST_DATADIR environment variable not found.\n");
  }

  // For ever loop
  for (;;) {
    // Read file
    i=0;
    file=fopen("schedule.txt","r");
    while (fgetline(file,line,LIM)>0) {
      sscanf(line,"%s %s %s %s",obs[i].stime,obs[i].sra,obs[i].sde,obs[i].camname);
      obs[i].ptime=decode_time(obs[i].stime);
      
      i++;
    }
    fclose(file);
    nobs=i;

    // Get local time
    time(&rawtime);
    //rawtime-=3600;
    

    // Print UTC time
    ptm=gmtime(&rawtime);
    strftime(buf,20,"%Y-%m-%dT%H:%M:%S",ptm);

    // Make raw time UTC to compare with scheduled time
    rawtime=mktime(ptm);

    // Show current raw time, just to check
    // printf("%s\n",ctime(&rawtime));

    // Compute time differences
    for (i=0;i<nobs;i++) 
      obs[i].dt=difftime(obs[i].ptime,rawtime);

    // Loop over observations
    for (i=0;i<nobs;i++) {
      if (obs[i].dt>0.0) {
	printf("%4.0f %s %s %s\n",obs[i].dt,obs[i].stime,obs[i].sra,obs[i].sde);
	break;
      } else if (obs[i].dt==0) {
	printf("Slewing to %s %s\n",obs[i].sra,obs[i].sde);
	send_position(obs[i].sra,obs[i].sde, obs[i].camname);
      }
    }

    // Sleep
    sleep(1);
  }

  return 0;
}

// Read a line of maximum length int lim from file FILE into string s
int fgetline(FILE *file,char *s,int lim)
{
  int c,i=0;
 
  while (--lim > 0 && (c=fgetc(file)) != EOF && c != '\n')
    s[i++] = c;
  if (c == '\n')
    s[i++] = c;
  s[i] = '\0';
  return i;
}

// Read data/cameras.txt in search of specified camera name and return complete camera details line
int read_cameras(char *camname, char *camera)
{
  FILE *file;
  char line[127],filename[127];

  sprintf(filename,"%s/data/cameras.txt",datadir);
  file=fopen(filename,"r");
  if (file==NULL) {
    printf("File with camera information not found!\n");
    return -1;
  }
  while (fgets(line,LIM,file)!=NULL) {
    // Skip commented lines
    if (strstr(line,"#")!=NULL)
      continue;
    if(strstr(line, camname)!=NULL){
      strcpy(camera, line);
      return 0;
    }
  }
  fclose(file);
  return -1;
}


// Send new position to telescope
void send_position(char *sra,char *sde,char *camname)
{
  int skt, port;
  struct hostent *he;
  struct sockaddr_in addr;
  char packet[LIM];
  FILE *file;
  float ra,de;
  char camera[127];


  // Old packet style
  //  sprintf(packet,"<newNumberVector device='Celestron GPS' name='EQUATORIAL_EOD_COORD_REQUEST'><oneNumber name='RA'>%s</oneNumber><oneNumber name='DEC'>%s</oneNumber></newNumberVector>",sra,sde);

  // New packet style (as of 2013-08-20)
  sprintf(packet,"<newNumberVector device='Celestron GPS' name='EQUATORIAL_EOD_COORD'><oneNumber name='RA'>%s</oneNumber><oneNumber name='DEC'>%s</oneNumber></newNumberVector>",sra,sde);

  // Send TCP packet
  skt=socket(AF_INET,SOCK_STREAM,0);
  addr.sin_family=AF_INET;
  port=PORT;
  addr.sin_port=htons(port);
  he=gethostbyname(IP);
  bcopy(he->h_addr,(struct in_addr *) &addr.sin_addr,he->h_length);
  while((connect(skt,(struct sockaddr *) &addr,sizeof(addr))<0) && (port < MAXPORT)) {
    fprintf(stderr,"Connection refused by remote host on port %04d.\n",port);
    port++;
    // Skip port 7265... used by some other service?
    if(port==7265) port++;
    fprintf(stderr,"Trying port %04d.\n",port);

    addr.sin_port=htons(port);
    he=gethostbyname(IP);
    bcopy(he->h_addr,(struct in_addr *) &addr.sin_addr,he->h_length);

  }
  if(port>=MAXPORT) return;

  printf("Connected to Indi server on port %04d.\n",port);

  write(skt,packet,strlen(packet));
  close(skt); 
 
  // Set restart
  file=fopen("/data1/satobs/control/state.txt","w");
  if (file!=NULL) {
    fprintf(file,"restart");
    fclose(file);
  }

  // Set position
  file=fopen("/data1/satobs/control/position.txt","w");
  if (file!=NULL) {
    fprintf(file,"%s %s\n",sra,sde);
    fclose(file);
  }

  // Set camera
  // camera.txt control file with complete line from data/cameras.txt describing the scheduled camera
  read_cameras(camname, camera);  // search for camera name
  file=fopen("/data1/satobs/control/camera.txt","w");
  if (file!=NULL) {
    fprintf(file,"%s\n",camera);
    fclose(file);
  }

  return;
}

// Decode time
time_t decode_time(char *stm)
{
  time_t aimtime;
  struct tm *rtm;
  int d;

  rtm=gmtime(&aimtime);
  sscanf(stm,"%04d-%02d-%02dT%02d:%02d:%02d",&rtm->tm_year,&rtm->tm_mon,&rtm->tm_mday,&rtm->tm_hour,&rtm->tm_min,&rtm->tm_sec);
  rtm->tm_year-=1900;
  rtm->tm_mon--;
  aimtime=mktime(rtm);

  return aimtime;
}
