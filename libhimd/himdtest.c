#include <stdio.h>
#include <glib.h>
#include <locale.h>
#include <string.h>

#include "himd.h"

char * get_locale_str(struct himd * himd, int idx)
{
    char * str, * outstr;
    if(idx == 0)
        return NULL;

    str = himd_get_string_utf8(himd, idx, NULL, NULL);
    if(!str)
        return NULL;

    outstr = g_locale_from_utf8(str,-1,NULL,NULL,NULL);
    himd_free(str);
    return outstr;
}

void himd_trackdump(struct himd * himd, int verbose)
{
    int i;
    struct himderrinfo status;
    for(i = HIMD_FIRST_TRACK;i <= HIMD_LAST_TRACK;i++)
    {
        struct trackinfo t;
        if(himd_get_track_info(himd, i, &t, NULL)  >= 0)
        {
            char *title, *artist, *album;
            title = get_locale_str(himd, t.title);
            artist = get_locale_str(himd, t.artist);
            album = get_locale_str(himd, t.album);
            printf("%4d: %d:%02d %s %s:%s (%s %d)\n",
                    i, t.seconds/60, t.seconds % 60, himd_get_codec_name(&t),
                    artist ? artist : "Unknown artist", 
                    title ? title : "Unknown title",
                    album ? album : "Unknown album", t.trackinalbum);
            g_free(title);
            g_free(artist);
            g_free(album);
            if(verbose)
            {
                struct fraginfo f;
                int fnum = t.firstfrag;
                while(fnum != 0)
                {
                    if(himd_get_fragment_info(himd, fnum, &f, &status) >= 0)
                    {
                        printf("     %3d@%05d .. %3d@%05d\n", f.firstframe, f.firstblock, f.lastframe, f.lastblock);
                        fnum = f.nextfrag;
                    }
                    else
                    {
                        printf("     ERROR reading fragment %d info: %s\n", fnum, status.statusmsg);
                        break;
                    }
                }
                printf("     Key: %02x%02x%02x%02x%02x%02x%02x%02x; MAC: %02x%02x%02x%02x%02x%02x%02x%02x\n",
                        t.key[0],t.key[1],t.key[2],t.key[3],t.key[4],t.key[5],t.key[6],t.key[7],
                        t.mac[0],t.mac[1],t.mac[2],t.mac[3],t.mac[4],t.mac[5],t.mac[6],t.mac[7]);
            }
        }
    }
}

void himd_stringdump(struct himd * himd)
{
    int i;
    struct himderrinfo status;
    for(i = 1;i < 4096;i++)
    {
        char * str;
        int type;
        if((str = himd_get_string_utf8(himd, i, &type, &status)) != NULL)
        {
            char * typestr;
            char * outstr;
            switch(type)
            {
                case STRING_TYPE_TITLE: typestr="Title"; break;
                case STRING_TYPE_ARTIST: typestr="Artist"; break;
                case STRING_TYPE_ALBUM: typestr="Album"; break;
                case STRING_TYPE_GROUP: typestr="Group"; break;
                default: typestr=""; break;
            }
            outstr = g_locale_from_utf8(str,-1,NULL,NULL,NULL);
            printf("%4d: %-6s %s\n", i, typestr, outstr);
            g_free(outstr);
            himd_free(str);
        }
        else if(status.status != HIMD_ERROR_NOT_STRING_HEAD)
            printf("%04d: ERROR %s\n", i, status.statusmsg);
    }
}

void himd_dumpdiscid(struct himd * h)
{
    int i;
    struct himderrinfo status;
    const unsigned char * discid = himd_get_discid(h, &status);
    if(!discid)
    {
        fprintf(stderr,"Error obtaining disc ID: %s", status.statusmsg);
        return;
    }
    printf("Disc ID: ");
    for(i = 0;i < 16;++i)
        printf("%02X",discid[i]);
    puts("");        
}

void himd_dumptrack(struct himd * himd, int trknum)
{
    struct trackinfo t;
    struct himd_blockstream str;
    struct himderrinfo status;
    FILE * strdumpf;
    unsigned int firstframe, lastframe;
    unsigned char block[16384];
    int blocknum = 0;
    strdumpf = fopen("stream.dmp","wb");
    if(!strdumpf)
    {
        perror("Opening stream.dmp");
        return;
    }
    if(himd_get_track_info(himd, trknum, &t, &status) < 0)
    {
        fprintf(stderr, "Error obtaining track info: %s\n", status.statusmsg);
        return;
    }
    if(himd_blockstream_open(himd, t.firstfrag, &str, &status) < 0)
    {
        fprintf(stderr, "Error opening stream %d: %s\n", t.firstfrag, status.statusmsg);
        return;
    }
    while(himd_blockstream_read(&str, block, &firstframe, &lastframe, &status) >= 0)
    {
        if(fwrite(block,16384,1,strdumpf) != 1)
        {
            perror("writing dumped stream");
            goto clean;
        }
        printf("%d: %u..%u\n",blocknum++,firstframe,lastframe);
    }
    if(status.status != HIMD_STATUS_AUDIO_EOF)
        fprintf(stderr,"Error reading MP3 data: %s\n", status.statusmsg);
clean:
    fclose(strdumpf);
    himd_blockstream_close(&str);
}

void himd_dumpmp3(struct himd * himd, int trknum)
{
    struct himd_mp3stream str;
    struct himderrinfo status;
    FILE * strdumpf;
    unsigned int len;
    const unsigned char * data;
    strdumpf = fopen("stream.mp3","wb");
    if(!strdumpf)
    {
        perror("Opening stream.mp3");
        return;
    }
    if(himd_mp3stream_open(himd, trknum, &str, &status) < 0)
    {
        fprintf(stderr, "Error opening track %d: %s\n", trknum, status.statusmsg);
        return;
    }
    while(himd_mp3stream_read_frame(&str, &data, &len, &status) >= 0)
    {
        if(fwrite(data,len,1,strdumpf) != 1)
        {
            perror("writing dumped stream");
            goto clean;
        }
    }
    if(status.status != HIMD_STATUS_AUDIO_EOF)
        fprintf(stderr,"Error reading MP3 data: %s\n", status.statusmsg);
clean:
    fclose(strdumpf);
    himd_mp3stream_close(&str);
}

int main(int argc, char ** argv)
{
    int idx;
    struct himd h;
    struct himderrinfo status;
    setlocale(LC_ALL,"");
    if(argc < 2)
    {
        fputs("Please specify mountpoint of image\n",stderr);
        return 1;
    }
    if(himd_open(&h,argv[1], &status) < 0)
    {
        puts(status.statusmsg);
        return 1;
    }
    if(argc == 2 || strcmp(argv[2],"strings") == 0)
        himd_stringdump(&h);
    else if(strcmp(argv[2],"tracks") == 0)
        himd_trackdump(&h, argc > 3);
    else if(strcmp(argv[2],"discid") == 0)
        himd_dumpdiscid(&h);
    else if(strcmp(argv[2],"mp3key") == 0 && argc > 3)
    {
        mp3key k;
        idx = 1;
        sscanf(argv[3], "%d", &idx);
        himd_obtain_mp3key(&h, idx, &k, NULL);
        printf("Track key: %02x%02x%02x%02x\n", k[0], k[1], k[2], k[3]);
    }
    else if(strcmp(argv[2],"dumptrack") == 0 && argc > 3)
    {
        idx = 1;
        sscanf(argv[3], "%d", &idx);
        himd_dumptrack(&h, idx);
    }
    else if(strcmp(argv[2],"dumpmp3") == 0 && argc > 3)
    {
        idx = 1;
        sscanf(argv[3], "%d", &idx);
        himd_dumpmp3(&h, idx);
    }

    himd_close(&h);
    return 0;
}
