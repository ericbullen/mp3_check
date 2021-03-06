#include <sys/times.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include "mp3_check.h"
#include "support_functions.h"

//  Function prototypes go here. 
inline int		move_to_next_frame(char *possible_mp3_tag, frame_info *mp3_i, gen_info *file_info, command_flags *flags, FILE *);
int			get_char_from_file(FILE *, unsigned int *header_value, gen_info *file_info, command_flags *flags, char *possible_mp3_tag);
int			scan_file(FILE *, char *filename, meta_options *flag_options, command_flags *flags);
int			parse_args(char **argv, meta_options *flag_options, command_flags *flags);
int 			crc_check_frame(char *data_frame, frame_info *mp3_i);
void			init_command_flags_struct(command_flags *flags);
void			init_frame_struct(frame_info *FI);
void			print_summary(frame_info mp3_i, char *filename);
void			print_usage(void);
extern int		check_header_value(unsigned int *header, char *filename, frame_info *FI);
extern int		check_vbr_and_time(frame_info *mp3_i, vbr_data *vbr_info, gen_info *file_info);
extern int		copy_int_array_to_str(char *possible_mp3_tag, char *tag_field, int offset, int length, int max_length);
extern int		dump_id3_tag(id3_tag_info *id3_tag);
extern int		get_last_char_offset(char *fat_string);
extern int		print_frame_info(frame_info *mp3_i, gen_info *file_info);
extern int		rotate_char_array(char *byte_list, int *new_byte, gen_info *file_info);
extern int		transform_char_array(char *byte_list, gen_info *file_info);
extern int		validate_id3_tag(char *possible_mp3_tag, id3_tag_info *id3_tag);
extern void		init_id3_tag_struct(id3_tag_info *TAG);
extern void		print_sys_usage(void);
extern void		translate_time(gen_info *file_info, mp3_time *song_time);
extern void		init_vbr_tag_struct(vbr_data *vbr_info);
extern void		init_mp3_time_struct(mp3_time *song_time);
extern void		init_gen_info_struct(gen_info *file_info);
extern inline int	cmp_str(const char *full_str, const char *chk_str, int full_str_offset);

int
scan_file(fp, filename, flag_options, flags)
	register FILE 	*fp;
	char		*filename;
	meta_options 	*flag_options;
	command_flags 	*flags;
{
	int		counter = 0;
	int		END_OF_FILE = FALSE;
	int		found_first_frame = FALSE;
	char		possible_mp3_tag[BUFFER_LENGTH];
	unsigned int	header_value = 0;

	//  This is set to 'YES' so that I will always get 
	//  the first 4 bytes off the stream no matter what. 
	
	char	found_valid_header = YES;
	char	found_weak_header = NO;


	// Keep all the structures centralized.
	frame_info	mp3_i;
	vbr_data	vbr_info;
	mp3_time	song_time;
	frame_info	first_mp3_frame;
	id3_tag_info	id3_tag;
	gen_info 	file_info;


	//  Zero these structures out. 
	init_frame_struct(&first_mp3_frame);
	init_vbr_tag_struct(&vbr_info);
	init_gen_info_struct(&file_info);


	while   (!END_OF_FILE)  {
		if (flags->bflag || flags->aflag) {

			//  This is the part where if the current byte count 
			//  is greater than the upper limit on the commandline, 
			//  it will break out. 

			if (flags->bflag && (file_info.byte_count > flag_options->byte_limit))
				break;
		}

		//  
		//  The fist run defaults to 'YES', so the 'else' section 
		//  runs first 
		//  
	
		//  The logic below is what travels through the mp3 file, 
		//  and then quickly does a sync checksum to see if it is 
		//  a candidate. 

		if (!found_valid_header) {
			// Just add one character to the end 
			// and keep the rest. I do not want to grab 
			// another 4 bytes unless I got a success. 
		
			//  Putting the below action in a function slows it down quite a bit, 
			//  but, it makes the code more readable.	 

                        if (get_char_from_file(fp, &header_value, &file_info, flags, possible_mp3_tag)) {
                        	if (!flags->ssflag && flags->sflag && !flags->fflag) {
                        		(void)fprintf(stdout, "%c", (header_value & 0xff));	
				}
                        } else
                       		break;
		} else {
			//  Clear it out of old values. 
			header_value = 0;
	
			//  Grab the complete 4-byte header (if it is one).

			for (counter = 0 ; counter < 4 ; counter++) {	
				if (get_char_from_file(fp, &header_value, &file_info, flags, possible_mp3_tag)) {
					if (!flags->ssflag && flags->sflag && !flags->fflag) {
						(void)fprintf(stdout, "%c", (header_value & 0xff));
					}
				} else
					break;
			}
		}


		//  
		//  Check if this is a valid frame sync 
		//  
		if (((header_value >> 21) & 0x7ff) == 0x7ff) {

			found_weak_header = YES;

			init_frame_struct(&mp3_i);

			if (check_header_value(&header_value, filename, &mp3_i)) {
				found_valid_header = YES;

				//  
				//  Was the offset of this current frame known 
				//  by the previous frame? It better have... 
				//  
				//  This also defines junk at the beginning as being a bad frame. 
				//  
				if ((file_info.good_frame_count > 0) && (file_info.next_expected_frame != file_info.byte_count)) {
					file_info.bad_frame_count++;

					// I guess if I get to this section,
					// I have found an mp3 with some bad
					// stuff in it, so I will return a      
					// fail when the program exits.

					(void)fprintf(stderr, "\nAn expected frame was not found. Expected it at offset 0x%x (BYTE %d), now at offset 0x%x (BYTE %d).", 
						file_info.next_expected_frame, file_info.next_expected_frame, file_info.byte_count, file_info.byte_count);

					// Checking to see if a contiguous amount of frames
					// were found. This check only occurs when a bad
					// frame was found.
					if (flags->qflag && (file_info.frame_sequence_count > 0) && (flag_options->min_frame_seq > file_info.frame_sequence_count)) {
						(void)fprintf(stderr, "\nMininum contiguous number of frames wasn't reached. Got to %d, needed %d at offset 0x%x (BYTE %d).", 
							file_info.frame_sequence_count, flag_options->min_frame_seq, file_info.next_expected_frame, file_info.next_expected_frame);

						file_info.frame_sequence_count = 0;
					}

				} else if (file_info.next_expected_frame == file_info.byte_count) {
					file_info.good_frame_count++;
					file_info.frame_sequence_count++;
				} else
					printf("\nSomething happened at byte %d. Next expected frame: %d.\n", file_info.byte_count, file_info.next_expected_frame);


				//  
				//  The fflag is only good to use when there is *no* valid data,
				//  and not when there is good data (it should be implied). 
				//  !sflag is set up this way because the data is already being
				//  shown above.
				// 
				//  There are two parts to this header printing.

                                if (!flags->ssflag && (flags->sflag && flags->fflag))
                                        (void)fprintf(stdout, "%c%c%c%c",	((header_value >> 24) & 0xff),
										((header_value >> 16) & 0xff),
										((header_value >> 8)  & 0xff),
										(header_value & 0xff));

				// I'm currently in a known good frame,
				// so I can safely move forward two characters
				// and still be in the frame. I will have to 
				// compensate when I move to the next frame.
				// That is taken care of in move_to_next_frame.
				
				if (mp3_i.PROT_BIT) {
					// Now, to grab the 2 bytes for the CRC value.
					for (counter = 0 ; counter < 2 ; counter++) {	
						if (get_char_from_file(fp, &header_value, &file_info, flags, possible_mp3_tag)) {
							if (!flags->ssflag && flags->sflag && !flags->fflag) {
								(void)fprintf(stdout, "%c", (header_value & 0xff));
							}
						} else {
							break;
						}
					}
					
					// Now, extract it from the header.
					mp3_i.CRC16_VALUE = header_value & 0xffff;


					//  
					//  The fflag is only good to use when there is *no* valid data,
					//  and not when there is good data (it should be implied). 
					//  !sflag is set up this way because the data is already being
					//  shown above.
					//

                                	if (!flags->ssflag && (flags->sflag && flags->fflag))
                                	        (void)fprintf(stdout, "%c%c",	((header_value >> 8)  & 0xff),
										(header_value & 0xff));
				}




				// If the header is valid, but has bad data, 
				// it will still store this information. Perhaps
				// it would be better to make sure it *really* is
				// a good frame before storing any VBR data...

				check_vbr_and_time(&mp3_i, &vbr_info, &file_info);


				//  
				//  I always want to keep the first frame 
				//  so I can print it out at the end 
				//  consistently. 
				//  
				// I am going to disable the 'if' statement
				// because for some reason if there's an anomaly
				// in the frame, it keeps the data, so I guess
				// for now I'll just have this information
				// updated at every frame.
				//
				if (file_info.good_frame_count > 0) {
					first_mp3_frame = mp3_i;
					found_first_frame = TRUE;
				}


				//  Print out per-frame stats. 
				if (!flags->sflag && flags->vvflag && (file_info.good_frame_count > 0))
					print_frame_info(&mp3_i, &file_info);


				//  
				//  This will checkety-check to see if I get the next frame 
				//  when I am supposed to. 
				//  
				if (mp3_i.PROT_BIT)
					file_info.next_expected_frame = file_info.byte_count + mp3_i.FRAME_LENGTH - 2;
				else
					file_info.next_expected_frame = file_info.byte_count + mp3_i.FRAME_LENGTH;


				//  Keep on searching the whole mp3? Ok, lemme skip though the data 
				//  to get to the next header. 
				if (flags->aflag || (flags->bflag && (flag_options->byte_limit > 0))) {
					if (!move_to_next_frame(possible_mp3_tag, &mp3_i, &file_info, flags, fp))
						break;
				} else
					//  I found my first valid header, so I can quit now. 
					break;

			} else {
				// I guess if I get to this section,
				// I have found an mp3 with some bad
				// stuff in it, so I will return a 
				// fail when the program exits.

				file_info.bad_frame_count++;

				if (flags->vflag)
					(void)fprintf(stderr, "\nA possible header 0x%x passed the weak sieve, but failed the strong one at offset 0x%x (BYTE %d).", 
						header_value, file_info.byte_count, file_info.byte_count);


				if (flags->qflag && (file_info.frame_sequence_count > 0) && (flag_options->min_frame_seq > file_info.frame_sequence_count)) {
					(void)fprintf(stderr, "\nMininum contiguous number of frames wasn't reached. Got to %d, needed %d at offset 0x%x (BYTE %d).", 
						file_info.frame_sequence_count, flag_options->min_frame_seq, file_info.next_expected_frame, file_info.next_expected_frame);

					file_info.frame_sequence_count = 0;
				}	
			}

		} else if (file_info.file_pos == 4 && (header_value & 0xffffff00) == 0x49443300){


			// Getting to this section means we may have an ID3V2.x.x header
			(void)fprintf(stderr, "Possible ID3v2 frame found, skipping\n");

			// Since this section is going to be under serious development,
			// the flag 'eflag' will allow mp3_check to record a bad frame
			// when an id3v2 tag is found. This will allow the user to weed
			// out troublesome id3v2 mp3s.

			mp3_i.ID3V2 = TRUE;

			if (flags->eflag) {
				if (flags->qflag && (file_info.frame_sequence_count > 0) && (flag_options->min_frame_seq > file_info.frame_sequence_count)) {
					(void)fprintf(stderr, "\nMininum contiguous number of frames wasn't reached. Got to %d, needed %d at offset 0x%x (BYTE %d).", 
						file_info.frame_sequence_count, flag_options->min_frame_seq, file_info.next_expected_frame, file_info.next_expected_frame);

					file_info.frame_sequence_count = 0;
				}	

				file_info.bad_frame_count++;
			}			


			// TODO: Full ID3V2 checking & processing instead of skipping

			// Throw away the next two bytes - the first one should always be > 0xff
			// The next 4 bytes represent the len encoded to avoid 0x80 bits set
			for (counter = 0 ; counter < 6 ; counter++) {	
				if (get_char_from_file(fp, &header_value, &file_info, flags, possible_mp3_tag)) {
					if (!flags->ssflag && flags->sflag && !flags->fflag) {
						(void)fprintf(stdout, "%c", (header_value & 0xff));
					}
				} else {
					break;
				}
			}

			// calculate the proper length
			counter = ((header_value>>3)&0xfe00000)
				| ((header_value>>2)&0x1fc000)
				| ((header_value>>1)&0x3f80)
				| (header_value&0x7f);

			// TODO: account for Unsynchronization!
			// we may won't skip all of it if Unsynchroized is set (0x80 in byte 5 of file)

			// account for the 10 byte header and the next header
			// this is to avoide excess messages during sync
			file_info.next_expected_frame = counter + 14;

			// skip the ID3V2 frame
			while (counter-- > 0){
				if (get_char_from_file(fp, &header_value, &file_info, flags, possible_mp3_tag)) {
					if (!flags->ssflag && flags->sflag && !flags->fflag) {
						(void)fprintf(stdout, "%c", (header_value & 0xff));
					}
				} else {
					break;
				}
			}

			// and mark as not currently synchronized - force it
			found_valid_header = YES;
			found_weak_header = NO;
		} else {
			// Getting to this section does not mean we got
			// an invalid mp3 file...

			found_valid_header = NO;
			found_weak_header = NO;
		}
	} // WHILE 


	if (!flags->sflag) {
		if (flags->pflag) {
			printf("%s %s\t%s %d\t%s %d\t%s %d", "FILE_NAME", filename, "GOOD_FRAMES",  file_info.good_frame_count, "BAD_FRAMES", file_info.bad_frame_count, "LAST_BYTE_CHECKED", file_info.byte_count);	
		} else {
			printf("\n");
			printf("%-20s%s\n", "FILE_NAME", filename);
			printf("%-20s%d\n", "GOOD_FRAMES",  file_info.good_frame_count);
			printf("%-20s%d\n", "BAD_FRAMES", file_info.bad_frame_count);
			printf("%-20s%d", "LAST_BYTE_CHECKED", file_info.byte_count);
		}

		if ((vbr_info.high_rate != vbr_info.low_rate) && (file_info.good_frame_count > 0)) {
			//  I do not want floating values in ave_rate. Seems silly
			//  to have a decimal point for that.
			vbr_info.ave_rate = vbr_info.sum_rate / file_info.good_frame_count;

			if (flags->pflag) {
				// I don't want any newlines for the below line because
				// the pflag option is to print it out all one one line

				printf("\t%s %d\t%s %d\t%s %d", "VBR_HIGH", vbr_info.high_rate, "VBR_LOW", vbr_info.low_rate, "VBR_AVERAGE", vbr_info.ave_rate);
			} else {
				printf("\n");
				printf("%-20s%d\n", "VBR_HIGH", vbr_info.high_rate);
				printf("%-20s%d\n", "VBR_LOW", vbr_info.low_rate);
				printf("%-20s%d", "VBR_AVERAGE", vbr_info.ave_rate);
			}
		}

		if (found_first_frame) {
			if (flags->vflag && !flags->pflag) {
				print_summary(first_mp3_frame, filename);
			}

			translate_time(&file_info, &song_time);
			
			if (flags->pflag)
				printf("\t%-20s%02u:%02u.%02u\n", "SONG_LENGTH", song_time.minutes, song_time.seconds, song_time.frac_second);
			else
				printf("\n%-20s%02u:%02u.%02u\n", "SONG_LENGTH", song_time.minutes, song_time.seconds, song_time.frac_second);
				
		} else {
			// This line below closes the strings above.
			// notice that at the end of the last string, there is no newline...
			printf("\n");
		}
	}


	init_id3_tag_struct(&id3_tag);
	transform_char_array(possible_mp3_tag, &file_info);


	if (flags->iflag && validate_id3_tag(possible_mp3_tag, &id3_tag)) {
		if (flags->iflag && flags->fflag && flags->sflag)
			dump_id3_tag(&id3_tag);

		if (!flags->sflag && flags->vflag) {
			printf("\n%-20s%d\n", "MP3_TAG", 1);

			printf("%-20s%s\n", "TITLE", id3_tag.TITLE);
			printf("%-20s%s\n", "ARTIST", id3_tag.ARTIST);
			printf("%-20s%s\n", "ALBUM", id3_tag.ALBUM);
			printf("%-20s%s\n", "YEAR", id3_tag.YEAR);
			printf("%-20s%s\n", "COMMENT", id3_tag.COMMENT);
			printf("%-20s%d\n", "GENRE", id3_tag.GENRE);
			printf("%-20s%d\n", "TRACK", id3_tag.TRACK_NUMBER);
			printf("%-20s%d\n", "ID3.11", id3_tag.ID3_311_VERSION);
		}
	} else if (!flags->sflag && flags->iflag) {
		printf("\n%-20s%d\n", "MP3_TAG", 0);
	}

	if (ferror(fp)) {
		fprintf(stderr, "mp3_check: %s %s\n", filename, strerror(errno));
		clearerr(fp);
	}

	if (ferror(stdout)) {
		fprintf(stderr, "mp3_check: stdout %s\n", strerror(errno));
		exit (1);
	}

	//
	// The 'file_info.bad_frame_count' seems
	// the best way to find errors,
	// so I will use it.
	//
	if (file_info.bad_frame_count > 0)
		return(FAIL);
	else
		return(PASS);

}

void
init_command_flags_struct(flags)
	command_flags *flags;
{
	flags->aflag 	= FALSE;
	flags->bflag 	= FALSE;
	flags->eflag	= FALSE;
	flags->fflag	= FALSE;
	flags->iflag 	= FALSE;
	flags->pflag	= FALSE;
	flags->qflag	= FALSE;
	flags->sflag 	= FALSE;
	flags->ssflag	= FALSE;
	flags->vflag 	= FALSE;
	flags->vvflag 	= FALSE;
}

void
print_summary(mp3_i, filename)
	frame_info	mp3_i;
	char	*filename;
{
	printf("\n");
	printf("%-20s%d\n", "TRUE", 1);
	printf("%-20s%d\n", "FALSE", 0);
	printf("%-20s%d\n", "MPV_1", mp3_i.MPV_1);
	printf("%-20s%d\n", "MPV_2", mp3_i.MPV_2);
	printf("%-20s%d\n", "MPV_25", mp3_i.MPV_25);
	printf("%-20s%d\n", "MPV_RESERVED", mp3_i.MPV_RESERVED);
	printf("%-20s%d\n", "L1", mp3_i.L1);
	printf("%-20s%d\n", "L2", mp3_i.L2);
	printf("%-20s%d\n", "L3", mp3_i.L3);
	printf("%-20s%d\n", "L_RESERVED", mp3_i.L_RESERVED);
	printf("%-20s%d\n", "PROT_BIT", mp3_i.PROT_BIT);
	printf("%-20s%d\n", "BIT_RATE", mp3_i.BIT_RATE);
	printf("%-20s%d\n", "SAMPLE_FREQ", mp3_i.SAMPLE_FREQ);
	printf("%-20s%d\n", "SAMPLES_PER_FRAME", mp3_i.SAMPLES_PER_FRAME);
	printf("%-20s%d\n", "PAD_BIT", mp3_i.PAD_BIT);
	printf("%-20s%d\n", "PRIV_BIT", mp3_i.PRIV_BIT);
	printf("%-20s%d\n", "STEREO", mp3_i.STEREO);
	printf("%-20s%d\n", "JOINT_STEREO", mp3_i.JOINT_STEREO);
	printf("%-20s%d\n", "DUAL_STEREO", mp3_i.DUAL_STEREO);
	printf("%-20s%d\n", "SINGLE_CHANNEL", mp3_i.SINGLE_CHANNEL);
	printf("%-20s%d\n", "MODE_EXTENSION", mp3_i.MODE_EXTENSION);
	printf("%-20s%d\n", "ID3V2", mp3_i.ID3V2);
	printf("%-20s%d\n", "FRAME_LENGTH", mp3_i.FRAME_LENGTH);
	printf("%-20s%d\n", "COPYRIGHT", mp3_i.COPYRIGHT);
	printf("%-20s%d\n", "ORIGINAL", mp3_i.ORIGINAL);
	printf("%-20s%d\n", "EMPH_NONE", mp3_i.EMPH_NONE);
	printf("%-20s%d\n", "EMPH_5015", mp3_i.EMPH_5015);
	printf("%-20s%d\n", "EMPH_RESERV", mp3_i.EMPH_RESERV);
	printf("%-20s%d\n", "EMPH_CCIT", mp3_i.EMPH_CCIT);
	printf("%-20s%d\n", "CHECK_STATE", mp3_i.check_state);
	printf("%-20s%s\n", "BIN_STRING", mp3_i.BIN_STRING);
}

void
init_frame_struct(FI)
	frame_info *FI;
{
	static frame_info empty_frame;
	static int inited = 0;

	if (!inited)
	{
		int i = 0;
		inited = 1;

		empty_frame.FRAME_LENGTH = 0;
		empty_frame.FRAME_DATA_LENGTH = 0;
		empty_frame.CRC16_VALUE = 0;
		empty_frame.CORRECT_CRC16_VALUE = 0;
		empty_frame.BIT_RATE = 0;
		empty_frame.SAMPLE_FREQ = 0;
		empty_frame.SAMPLES_PER_FRAME = 0;
		empty_frame.MPV_1 = 0;
		empty_frame.MPV_2 = 0;
		empty_frame.MPV_25 = 0;
		empty_frame.MPV_RESERVED = 0;
		empty_frame.L1 = 0;
		empty_frame.L2 = 0;
		empty_frame.L3 = 0;
		empty_frame.L_RESERVED = 0;
		empty_frame.PROT_BIT = 0;
		empty_frame.PAD_BIT = 0;
		empty_frame.PRIV_BIT = 0;
		empty_frame.STEREO = 0;
		empty_frame.JOINT_STEREO = 0;
		empty_frame.DUAL_STEREO = 0;
		empty_frame.SINGLE_CHANNEL = 0;
		empty_frame.MODE_EXTENSION = 0;
		empty_frame.ID3V2 = 0;
		empty_frame.COPYRIGHT = 0;
		empty_frame.ORIGINAL = 0;
		empty_frame.EMPH_NONE = 0;
		empty_frame.EMPH_5015 = 0;
		empty_frame.EMPH_RESERV = 0;
		empty_frame.EMPH_CCIT = 0;

		for (i=0 ; i < 32 ; i++)
			empty_frame.BIN_STRING[i] = '0';

		empty_frame.BIN_STRING[32] = '\0';

		empty_frame.INT_HEADER = 0;

		empty_frame.check_state = 0;
	}

	/* bitwise copy */
	*FI = empty_frame;
}

void
print_usage(void)
{
        (void)fprintf(stderr, "\nusage: mp3_check [-e] [-p] [-v[v]] [-a] [-b[<byte_count>]] [-i] [-s[s]] [-h] [-] [file ...]\n");
	(void)fprintf(stderr, "     -q<number>        Determines the mininum <number> of contiguous frames\n");
	(void)fprintf(stderr, "                       that must be present for an error NOT to occur.\n");
	(void)fprintf(stderr, "                       Analyzing random data will find spurious MP3 headers,\n");
	(void)fprintf(stderr, "                       but finding <number> sequential headers are even\n");
	(void)fprintf(stderr, "                       harder to find.\n\n");
	(void)fprintf(stderr, "     -e                While id3v2 support is under development, this flag will\n");
	(void)fprintf(stderr, "                       allow you to have mp3_check record a frame error when\n");
	(void)fprintf(stderr, "                       a id3v2 tag is found.\n\n");
	(void)fprintf(stderr, "     -p                Shows just the essentials (what you get without the\n");
	(void)fprintf(stderr, "                       -v option) on a single line for easy parsing. The\n");
	(void)fprintf(stderr, "                       fields are separated by tabs, and the name/value pairs\n");
	(void)fprintf(stderr, "                       are separated by spaces.\n\n");
	(void)fprintf(stderr, "     -v[v]             Lists details about the mp3 in name -> value order.\n");
	(void)fprintf(stderr, "                       Adding an extra 'v' at the end gives details about\n");
	(void)fprintf(stderr, "                       each frame. Extremly verbose!\n\n");
	(void)fprintf(stderr, "     -a                Checks the mp3 from stem to stern (default).\n");
	(void)fprintf(stderr, "                       If any other flags are present, it will not be\n");
	(void)fprintf(stderr, "                       enabled, and will instead behave as though the\n");
	(void)fprintf(stderr, "                       '-b' switch is present.\n\n");
	(void)fprintf(stderr, "     -b[<byte_count>]  If a frame is not found by <byte_count> bytes,\n");
	(void)fprintf(stderr, "                       quit, and return an error. If byte_count\n");
	(void)fprintf(stderr, "                       is not specified, it will search until the\n");
	(void)fprintf(stderr, "                       first valid frame and quit.\n\n");
	(void)fprintf(stderr, "     -i                Check to see if there is a ID3 tag, and if so,\n");
	(void)fprintf(stderr, "                       display the info.\n\n");
	(void)fprintf(stderr, "     -s[s|f]           Sends the mp3 to stdout (for CGI applications).\n");
	(void)fprintf(stderr, "                       If an additional 's' is there, then no output is\n");
	(void)fprintf(stderr, "                       made, just errors are reported (super silent).\n");
	(void)fprintf(stderr, "                       With the 'f' used, only valid frames are sent\n");
	(void)fprintf(stderr, "                       to stdout ('f'ixing the mp3 is attempted). Also,\n");
	(void)fprintf(stderr, "                       it is important to note that the id3 tag will not\n");
	(void)fprintf(stderr, "                       be included in the 'f'ixed mp3. If you want it\n");
	(void)fprintf(stderr, "                       included, you have to include the '-i' option.\n\n");
	(void)fprintf(stderr, "     -h                Print this text including the version.\n\n");
	(void)fprintf(stderr, "* VERSION:  %s   \n", CURRENT_VERSION);
	(void)fprintf(stderr, "* HOMEPAGE: %s   \n", HOMEPAGE);
	(void)fprintf(stderr, "* AUTHOR :  %s   \n", AUTHOR);
	

        exit(1);
}

int
parse_args(argv, flag_options, flags)
	char		**argv;
	meta_options	*flag_options;
	command_flags 	*flags;
{
	register FILE *fp;
	char	*filename;
	int	error_count = 0;
	int	found_file = FALSE;


	do {
		if (*argv) {
			if (strcmp(*argv, "-") == 0) {
				found_file = TRUE;
				filename = "stdin";
				fp = stdin;
				++argv;

			} else if ((fp = fopen(*argv, "rb")) != NULL) {
				found_file = TRUE;
				filename = *argv++;

			} else {
				fprintf (stderr, "mp3_check: %s %s\n", *argv, strerror(errno));
				++argv;
				continue;
			}

		}

		if (found_file) {
			if (!scan_file(fp, filename, flag_options, flags))
				error_count++;

			if (fp != stdin)
				(void)fclose(fp);

			found_file = FALSE;
		}

	} while (*argv);
	
	if (error_count > 0)
		return(FAIL);
	else
		return(PASS);
}

inline int
move_to_next_frame(possible_mp3_tag, mp3_i, file_info, flags, fp)
	char		*possible_mp3_tag;
	frame_info	*mp3_i;
	gen_info	*file_info;
	command_flags   *flags;
	FILE		*fp;
{
	int counter = 0;
	int unk_char = 0;
	int actual_bytes_read = 0;
	register char	*data_frame;

	const int print_to_stdout = !flags->ssflag && (flags->sflag || flags->fflag);
	int bytes_to_read = 0;

	// Here, mp3_check adjusts for the CRC checksum
	if (mp3_i->PROT_BIT)
		bytes_to_read = mp3_i->FRAME_LENGTH - 6; // 6 is for the offset after the header plus the two CRC bytes.
	else
		bytes_to_read = mp3_i->FRAME_LENGTH - 4; // 4 is for the offset after the header

	// The data_frame variable stores the contents
	// of the mp3 frame without the header. I can then
	// use this information to run crc16 analysis on it.

	data_frame = (char *) malloc (bytes_to_read);

	//  This for loop cycles though the frame data. All I want to know 
	//  is if the frame header is valid or not... 
	//  basically, I know I got a good frame, I just want to hurry 
	//  to the next one.

	// I am not sure if the '!print_to_stdout' is still useful here.
	// I should check on this later...

	if (fp!=stdin && !print_to_stdout && !flags->iflag) {
		if (mp3_i->PROT_BIT) {
			actual_bytes_read = fread (data_frame, sizeof(char), bytes_to_read, fp); 
	
			if (actual_bytes_read == bytes_to_read) {
				file_info->file_pos += bytes_to_read;
				file_info->byte_count += bytes_to_read;


				// May be useful later for working on the CRC stuff.
				//(void)fprintf(stdout,"Got in. Read %d bytes.\n", bytes_to_read);
				//(void)fprintf(stdout,"Data size is %d bytes.\n", actual_bytes_read);

				mp3_i->FRAME_DATA_LENGTH = actual_bytes_read;
				
				if (crc_check_frame(data_frame, mp3_i)) {
					(void)fprintf(stdout, "crc16 check passed.\n");
					
					free(data_frame);
					return(PASS);
				} else {
					// I am not sure why I have to do the '& 0xffff' at the end for only 
					// one crc16 value. Weird.

					//(void)fprintf(stderr, "BROKEN_CODE: The crc16 checksum failed. Expected 0x%x, got 0x%x.\n", mp3_i->CRC16_VALUE, mp3_i->CORRECT_CRC16_VALUE & 0xffff);

					// Since it doesn't work right yet, I'll leave this disabled.
					//return(FAIL);
				}
			} else {
				(void)fprintf(stderr,"File error while trying to read frame into memory.\n");

				free(data_frame);
				return(FAIL);
			}
		} else {
			if (!fseek (fp, bytes_to_read, SEEK_CUR)) {
				file_info->file_pos += bytes_to_read;
				file_info->byte_count += bytes_to_read;
		
				free(data_frame);
				return(PASS);
			}
		}
	} else {

		//  Walk though the contents by hand if we've got to print the
		//  contents, or if we're reading from stdin, or if something
		//  went wrong with fseek().


		for (counter = 0 ; counter < bytes_to_read ; counter++) {
			if ((unk_char = getc(fp)) != EOF) {

				++file_info->file_pos;
				++file_info->byte_count;

				data_frame[counter] = unk_char;


				// This is to keep track of the id3
				// tag that is at the end of mp3s.

				if (flags->iflag)
					rotate_char_array(possible_mp3_tag, &unk_char, file_info);

				//  Since I know I am in a good frame, print out the data. 
				//  When I am in this function, it doesnt matter if the fflag 
				//  is set. 

				if (print_to_stdout)
					(void) fprintf (stdout, "%c", unk_char );

			} else {

				free(data_frame);
				return(FAIL);
				break;
			}
		}	
	}

	free(data_frame);
	return(PASS);
}

int
crc_check_frame(data_frame, mp3_i)
	char		*data_frame;
	frame_info	*mp3_i;
{   
	///////////////////////////////////////
	// NOTE: This function works correctly
	///////////////////////////////////////
	
	// CRC-16 table. Nice and fast.
	static short crc_table[] = {
		0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
		0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
		0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
		0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
		0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
		0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
		0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
		0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
		0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
		0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
		0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
		0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
		0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
		0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
		0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
		0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
		0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
		0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
		0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
		0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
		0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
		0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
		0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
		0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
		0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
		0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
		0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
		0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
		0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
		0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
		0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
		0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
	};

        register int 	i = 0;
        register short 	crc = 0;
	int 		size_of_data = mp3_i->FRAME_DATA_LENGTH;

	// For testing value of 0x4c44 is returned (correct).
	//strcpy(data_frame, "This is a string");

        for (i=0 ; i < size_of_data; i++)
                crc = ((crc >> 8) & 0xff) ^ crc_table[(crc ^ *data_frame++) & 0xff];
        

	mp3_i->CORRECT_CRC16_VALUE = crc & 0xffff;


	if (mp3_i->CRC16_VALUE == mp3_i->CORRECT_CRC16_VALUE)
		return(PASS);
	else
		return(FAIL);
}

int
get_char_from_file(fp, header_value, file_info, flags, possible_mp3_tag)
	register FILE   *fp;
	unsigned int	*header_value;
	command_flags	*flags;
	gen_info	*file_info;
	char		*possible_mp3_tag;
{
	int		step_char = 0;
	
	if ((step_char = getc(fp)) != EOF) {
		// This keeps the 32 bit header, and rotates out
		// the oldest, and adds a new one.

		*header_value = (*header_value << 8) + step_char;

		++file_info->file_pos;
		++file_info->byte_count;
		
		// This section keeps track of the last 128 Bytes of the stream
		// to later check for mp3 tags.	
		if (flags->iflag)
			rotate_char_array(possible_mp3_tag, &step_char, file_info);
		
		return(PASS);
	} else
		return(FAIL);	
}
