#include <stdint.h>
#include "write.hpp"
#include "../System64/fs.hpp" // Pulls in Node, fs_find, and fs_create
#include "../System64/drivers/keyboard.hpp"
#include "../System64/drivers/nyxfs.hpp" // Pulls in NyxFS::sync()
#include "../System64/drivers/fat.hpp"   // Pulls in FAT:: create, delete, and write routines

extern "C" {
    void putchar(char c);
    void print(const char* str);
    void clear_workspace(); 
}

extern int col;
extern int row;
extern uint8_t color;
extern volatile uint16_t* vga;

static const int VGA_WIDTH = 80;

// Internal structural mapping for working with files inside notepad
struct NotepadSession {
    const char* filename;
    char* content_ptr;
    int max_capacity;
    int current_len;
    bool is_fat_mode;
};

// Core Interactive Text Editing Engine Loop
static void run_notepad_editor(NotepadSession* session) {
    clear_workspace();

    print("--- DOSnyx Notepad v3.0 ---\n");
    print("Editing target segment: "); print(session->filename);
    if (session->is_fat_mode) {
        print(" (FAT Volume Mode)\n");
    } else {
        print(" (Native RAM Mode)\n");
    }
    print("[ESC]: Save and Return to Main Panel | [BACKSPACE]: Delete\n");
    print("----------------------------------------------------------\n\n");

    // Print existing file text content onto the screen space
    if (session->current_len > 0) {
        for (int i = 0; i < session->current_len; i++) {
            putchar(session->content_ptr[i]);
        }
    }

    while (true) {
        char c = keyboard_getchar();
        if (!c) continue;

        // [ESC]: Break editing session loop and trigger file saving maps
        if (c == 27) {
            if (session->current_len < session->max_capacity) {
                session->content_ptr[session->current_len] = '\0'; 
            }
            return;
        }

        // [BACKSPACE]: Handle tracking counters and wipe trailing screen characters
        if (c == '\b') {
            if (session->current_len > 0) {
                session->current_len--;
                if (col > 0) {
                    col--;
                } else if (row > 6) { // Block backspace cursor from breaking above Row 6 line split
                    row--;
                    col = VGA_WIDTH - 1;
                }
                vga[row * VGA_WIDTH + col] = (color << 8) | ' ';
            }
            continue;
        }

        // [TAB]: Space expander expansion sequence
        if (c == '\t') {
            for (int i = 0; i < 4; i++) {
                if (session->current_len < (session->max_capacity - 1)) {
                    session->content_ptr[session->current_len++] = ' ';
                    putchar(' ');
                }
            }
            continue;
        }

        // Standard Keyboard Character Streams Input
        if (session->current_len < (session->max_capacity - 1)) {
            session->content_ptr[session->current_len++] = c;
            putchar(c);
        }
    }
}

// Unified VFS Routing Architecture Manager
void write_app_start(const char* filename) {
    if (!filename || filename[0] == '\0') {
        print("Error: Specify a valid file target name sequence.\n");
        return;
    }

    NotepadSession session;
    session.filename = filename;
    session.max_capacity = 1024; // Standard 1KB editing boundary line
    session.current_len = 0;

    // -------------------------------------------------------------------------
    // DRIVE 1: Native RAM Node Architecture Allocation Mapping
    // -------------------------------------------------------------------------
    if (current_drive_id == 1) {
        session.is_fat_mode = false;

        // Try to locate an existing memory node block tracking element using fs_find
        Node* file_node = fs_find(filename);

        // If the file does not exist, automatically allocate a fresh node via fs_create
        if (!file_node) {
            file_node = fs_create(filename, false);
        }

        if (!file_node) {
            print("Error: Memory allocation failure establishing file node block.\n");
            return;
        }

        session.content_ptr = file_node->content;
        session.current_len = file_node->size;

        // Hand over target layout parameters right into the core UI framework loop
        run_notepad_editor(&session);

        // Save internal node size back to structural RAM bounds on completion
        file_node->size = session.current_len;
        NyxFS::sync(); // Keep virtual disk node mappings aligned with the platter
        
        print("\nChanges synchronized back to Native RAM allocation tree successfully.\n");
    }
    // -------------------------------------------------------------------------
    // DRIVE 2-4: Hard Platter FAT16 File Generation Storage Mapping
    // -------------------------------------------------------------------------
    else if (current_drive_id >= 2 && current_drive_id <= 4) {
        session.is_fat_mode = true;

        // Allocate a temporary stack buffer workspace to accumulate disk payloads safely
        char fat_workspace_buf[1024];
        for (int i = 0; i < 1024; i++) fat_workspace_buf[i] = 0;

        session.content_ptr = fat_workspace_buf;

        // [ADDED WORKSPACE FILLING CODE HERE]
        // Attempt to read existing text data from sectors prior to running editor
        // Note: Change 'read_file_data' to your actual FAT driver read function name if it differs!
        int bytes_read = FAT::read_file_data(filename, fat_current_cluster, (uint8_t*)fat_workspace_buf);
        if (bytes_read > 0) {
            session.current_len = bytes_read;
        } else {
            session.current_len = 0; // Defaults to empty if file is new or vacant
        }

        // Launch editor sandbox layout framework using stack tracking buffer parameters
        run_notepad_editor(&session);

        print("\nFlushing text stream parameters to physical FAT volume sectors... ");

        // Step A: Evict old structural footprints to clean the track for updating modifications safely
        FAT::delete_entry(filename, fat_current_cluster);

        // Step B: Instantiate a clean, raw directory entry block element
        if (!FAT::create_file(filename, false, fat_current_cluster)) {
            print("\nError: Could not reserve file table directory tracking bounds.\n");
            return;
        }

        // Step C: Stream finalized buffer layout parameters into data cluster sector positions
        if (FAT::write_file_data(filename, fat_current_cluster, (const uint8_t*)session.content_ptr, session.current_len)) {
            print("Done.\n");
        } else {
            print("\nError: Hardware failure writing sectors to Allocation Tables.\n");
        }
    }
    else {
        print("Error: Target volume assignment out of valid system drive specifications.\n");
    }
}