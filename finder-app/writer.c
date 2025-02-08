#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

int main(int argc, char *argv[]) {
	FILE *fp;
    	const char *writefile;
    	const char *writestr;

	// Initialize syslog logging
    	openlog("writer", LOG_PID, LOG_USER);
    
	// Check for proper number of arguments
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <path_to_file> <string_to_write>\n", argv[0]);
		syslog(LOG_ERR, "Invalid number of arguments provided");
		closelog();
		return 1; // Return 1 for error
    	}
    
	writefile = argv[1];
    	writestr = argv[2];
    
	// Attempt to open/create file for writing
        fp = fopen(writefile, "w");
    	if (fp == NULL) {
		fprintf(stderr, "Error opening file %s: %s\n", writefile, strerror(errno));
		syslog(LOG_ERR, "Failed to open or create file %s: %s", writefile, strerror(errno));
		closelog();
		return 1; // Return 1 for error
    	}
    
	// Log the writing action
	syslog(LOG_DEBUG, "Writing '%s' to %s", writestr, writefile);
    
	// Write the string to the file
	if (fputs(writestr, fp) == EOF) {
		fprintf(stderr, "Error writing to file %s\n", writefile);
		syslog(LOG_ERR, "Failed to write to file %s", writefile);
		fclose(fp);
		closelog();
		return 1; // Return 1 for error
    	}    
	
	// Close the file
	fclose(fp);
    
	// Close syslog
	closelog();
    
	return 0; // Success
}