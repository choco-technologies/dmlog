/**
 * @file test_file_transfer_e2e.c
 * @brief End-to-end test for file transfer functionality with GDB
 */

#include "dmlog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE (8 * 1024)
static char log_buffer[BUFFER_SIZE];

int main(int argc, char* argv[])
{
    printf("File Transfer End-to-End Test\n");
    printf("==============================\n\n");
    
    // Create dmlog context
    dmlog_ctx_t ctx = dmlog_create(log_buffer, BUFFER_SIZE);
    if(!ctx)
    {
        fprintf(stderr, "Failed to create dmlog context\n");
        return 1;
    }
    
    dmlog_puts(ctx, "File transfer test starting...\n");
    
    // Test 1: Send a file from firmware to PC
    printf("Test 1: Sending file test_send.txt to PC\n");
    
    // Create a test file
    FILE* test_file = fopen("test_send.txt", "w");
    if(test_file)
    {
        fprintf(test_file, "Hello from firmware!\n");
        fprintf(test_file, "This is a test file for dmlog file transfer.\n");
        fprintf(test_file, "Line 3\n");
        fclose(test_file);
        
        dmlog_puts(ctx, "Calling dmlog_sendf...\n");
        
        // Send the file
        bool result = dmlog_sendf(ctx, "test_send.txt", "received_from_fw.txt", 32);
        
        if(result)
        {
            dmlog_puts(ctx, "File sent successfully!\n");
            printf("SUCCESS: File sent to PC\n");
        }
        else
        {
            dmlog_puts(ctx, "File send failed!\n");
            printf("FAILED: File send operation failed\n");
        }
    }
    else
    {
        printf("SKIPPED: Could not create test file\n");
    }
    
    // Give monitor time to process
    sleep(1);
    
    // Test 2: Receive a file from PC
    printf("\nTest 2: Receiving file from PC\n");
    
    // Create a file on PC to send
    FILE* pc_file = fopen("test_recv.txt", "w");
    if(pc_file)
    {
        fprintf(pc_file, "Hello from PC!\n");
        fprintf(pc_file, "This file is being sent to firmware.\n");
        fclose(pc_file);
        
        dmlog_puts(ctx, "Calling dmlog_recvf...\n");
        
        // Receive the file
        bool result = dmlog_recvf(ctx, "received_from_pc.txt", "test_recv.txt", 32);
        
        if(result)
        {
            dmlog_puts(ctx, "File received successfully!\n");
            printf("SUCCESS: File received from PC\n");
            
            // Verify the file
            FILE* verify = fopen("received_from_pc.txt", "r");
            if(verify)
            {
                char line[256];
                printf("File contents:\n");
                while(fgets(line, sizeof(line), verify))
                {
                    printf("  %s", line);
                }
                fclose(verify);
            }
        }
        else
        {
            dmlog_puts(ctx, "File receive failed!\n");
            printf("FAILED: File receive operation failed\n");
        }
    }
    else
    {
        printf("SKIPPED: Could not create PC file\n");
    }
    
    dmlog_puts(ctx, "File transfer test complete.\n");
    
    // Clean up
    dmlog_destroy(ctx);
    
    printf("\nTest completed. Check monitor output for details.\n");
    
    return 0;
}
