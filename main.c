#include "editor.c"

static void show_document(void) {
    struct LineNode *node = head;
    int number = 1;
    printf("---- %d line(s)%s ----\n", total_lines, is_dirty ? ", modified" : "");
    while (node != NULL) {
        printf("%3d| %s\n", number++, node->text);
        node = node->next;
    }
    printf("cursor: line %d, column %d\n", cur_line, cur_col);
}

static void move_cursor(int line, int col) {
    struct LineNode *node = ln_at(line);
    if (node == NULL) {
        printf("no such line\n");
        return;
    }
    cur_line = line;
    cur_line_ptr = node;
    cur_col = col < 0 ? 0 : (col > node->len ? node->len : col);
}

static void print_help(void) {
    printf("i <text>      insert text at the cursor\n"
           "n             Enter (split line)\n"
           "b             backspace\n"
           "w             delete word\n"
           "g <line> <col> move cursor\n"
           "f <term>      search\n"
           "r <old> <new> replace all\n"
           "u             undo\n"
           "s <file>      save\n"
           "l <file>      load\n"
           "p             show document\n"
           "h             this help\n"
           "q             quit\n");
}

int main(void) {
    char line[512];
    init_editor();
    print_help();
    while (fgets(line, sizeof(line), stdin) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        char command = line[0];
        const char *argument = line[0] != '\0' && line[1] == ' ' ? line + 2 : "";
        if (command == 'q') {
            break;
        } else if (command == 'i') {
            for (const char *c = argument; *c != '\0'; c++) {
                insert_char(*c);
            }
        } else if (command == 'n') {
            enter_line();
        } else if (command == 'b') {
            delete_char();
        } else if (command == 'w') {
            delete_word();
        } else if (command == 'g') {
            int target_line = 1, target_col = 0;
            sscanf(argument, "%d %d", &target_line, &target_col);
            move_cursor(target_line, target_col);
        } else if (command == 'f') {
            search_text(argument);
        } else if (command == 'r') {
            char old_word[128] = "", new_word[128] = "";
            sscanf(argument, "%127s %127s", old_word, new_word);
            printf("%d replacement(s)\n", replace_all_text(old_word, new_word));
        } else if (command == 'u') {
            undo_action();
        } else if (command == 's') {
            save_file(argument);
        } else if (command == 'l') {
            load_file(argument);
        } else if (command == 'p') {
            show_document();
        } else {
            print_help();
        }
    }
    return 0;
}
