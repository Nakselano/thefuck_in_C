#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <sys/wait.h>

#define MAX_PATH_LEN 256
#define MAX_SUGGESTIONS 5


#define MIN(a, b) (((a) < (b)) ? (a) : (b))

typedef struct {
    char command[MAX_PATH_LEN];
    int distance;
} CommandSuggestion;


int levenshtein_distance(const char *s1, const char *s2) {
    int len1 = strlen(s1);
    int len2 = strlen(s2);
    int matrix[len1 + 1][len2 + 1];

    for (int x = 0; x <= len1; x++) {
        matrix[x][0] = x;
    }
    for (int y = 0; y <= len2; y++) {
        matrix[0][y] = y;
    }

    for (int x = 1; x <= len1; x++) {
        for (int y = 1; y <= len2; y++) {
            int cost = (s1[x - 1] == s2[y - 1]) ? 0 : 1;
            matrix[x][y] = MIN(
                MIN(matrix[x - 1][y] + 1, matrix[x][y - 1] + 1),
                matrix[x - 1][y - 1] + cost
            );
        }
    }

    return matrix[len1][len2];
}

int command_exists(const char *cmd) {
    if (strcmp(cmd, "cd") == 0 || strcmp(cmd, "echo") == 0 || strcmp(cmd, "exit") == 0 ||
        strcmp(cmd, "export") == 0 || strcmp(cmd, "pwd") == 0 || strcmp(cmd, "unset") == 0) {
        return 1;
    }

    char path[MAX_PATH_LEN];
    char *env_path = getenv("PATH");
    if (!env_path) {
        fprintf(stderr, "Failed to get PATH environment variable\n");
        return 0;
    }
    char *env_path_copy = strdup(env_path);
    if (!env_path_copy) {
        fprintf(stderr, "Failed to duplicate PATH string\n");
        return 0;
    }
    char *token = strtok(env_path_copy, ":");

    while (token != NULL) {
        snprintf(path, sizeof(path), "%s/%s", token, cmd);
        if (access(path, X_OK) == 0) {
            free(env_path_copy);
            return 1;
        }
        token = strtok(NULL, ":");
    }

    free(env_path_copy);
    return 0;
}

void find_closest_commands(const char *input, CommandSuggestion suggestions[], int *suggestion_count, char previous_suggestions[][MAX_PATH_LEN], int previous_count) {
    struct dirent *entry;
    DIR *dp;
    char *env_path = getenv("PATH");
    if (!env_path) {
        fprintf(stderr, "Failed to get PATH environment variable\n");
        *suggestion_count = 0;
        return;
    }
    char *env_path_copy = strdup(env_path);
    if (!env_path_copy) {
        fprintf(stderr, "Failed to duplicate PATH string\n");
        *suggestion_count = 0;
        return;
    }
    char *token = strtok(env_path_copy, ":");

    while (token != NULL) {
        dp = opendir(token);
        if (dp == NULL) {
            token = strtok(NULL, ":");
            continue;
        }

        while ((entry = readdir(dp))) {
            int is_previous = 0;
            for (int i = 0; i < previous_count; i++) {
                if (strcmp(previous_suggestions[i], entry->d_name) == 0) {
                    is_previous = 1;
                    break;
                }
            }
            if (is_previous) {
                continue;
            }

            int distance = levenshtein_distance(input, entry->d_name);
            if (*suggestion_count < MAX_SUGGESTIONS || distance < suggestions[MAX_SUGGESTIONS - 1].distance) {
                if (*suggestion_count < MAX_SUGGESTIONS) {
                    strncpy(suggestions[*suggestion_count].command, entry->d_name, MAX_PATH_LEN - 1);
                    suggestions[*suggestion_count].command[MAX_PATH_LEN - 1] = '\0';
                    suggestions[*suggestion_count].distance = distance;
                    (*suggestion_count)++;
                } else {
                    int max_idx = 0;
                    for (int i = 1; i < MAX_SUGGESTIONS; i++) {
                        if (suggestions[i].distance > suggestions[max_idx].distance) {
                            max_idx = i;
                        }
                    }
                    if (distance < suggestions[max_idx].distance) {
                        strncpy(suggestions[max_idx].command, entry->d_name, MAX_PATH_LEN - 1);
                        suggestions[max_idx].command[MAX_PATH_LEN - 1] = '\0';
                        suggestions[max_idx].distance = distance;
                    }
                }
                for (int i = 0; i < *suggestion_count - 1; i++) {
                    for (int j = i + 1; j < *suggestion_count; j++) {
                        if (suggestions[i].distance > suggestions[j].distance) {
                            CommandSuggestion temp = suggestions[i];
                            suggestions[i] = suggestions[j];
                            suggestions[j] = temp;
                        }
                    }
                }
            }
        }

        closedir(dp);
        token = strtok(NULL, ":");
    }

    free(env_path_copy);
}

void monitor_commands() {
    char input[MAX_PATH_LEN];
    char corrected_command[MAX_PATH_LEN * 2];
    char *command, *args;

    while (1) {
        printf("Enter a command: ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }

        input[strcspn(input, "\n")] = 0; 

        command = strtok(input, " ");
        args = strtok(NULL, "");

        if (!command) {
            fprintf(stderr, "No command entered\n");
            continue;
        }

        char *cmd_parts[MAX_PATH_LEN];
        int i = 0;
        cmd_parts[i++] = command;
        while (args && (cmd_parts[i] = strtok(args, " ")) != NULL) {
            args = NULL;
            i++;
        }

        int corrected = 0;
        int corrected_index = -1; 
        char previous_suggestions[MAX_SUGGESTIONS * 10][MAX_PATH_LEN]; 
        int previous_count = 0;

        for (int j = 0; j < i; j++) {
            while (!command_exists(cmd_parts[j])) {
                CommandSuggestion suggestions[MAX_SUGGESTIONS];
                int suggestion_count = 0;
                find_closest_commands(cmd_parts[j], suggestions, &suggestion_count, previous_suggestions, previous_count);

                if (suggestion_count > 0) {
                    printf("Did you mean one of these?\n");
                    for (int k = 0; k < suggestion_count; k++) {
                        printf("%d: %s\n", k + 1, suggestions[k].command);
                        strncpy(previous_suggestions[previous_count], suggestions[k].command, MAX_PATH_LEN - 1);
                        previous_suggestions[previous_count][MAX_PATH_LEN - 1] = '\0';
                        previous_count++;
                    }
                    printf("Choose the correct command number (0 to skip, 6 for more options): ");
                    int choice = 0;
                    if (scanf("%d", &choice) != 1 || choice < 0 || choice > 6) {
                        choice = 0;
                    }
                    while (getchar() != '\n'); 

                    if (choice > 0 && choice <= suggestion_count) {
                        strncpy(cmd_parts[j], suggestions[choice - 1].command, MAX_PATH_LEN - 1);
                        cmd_parts[j][MAX_PATH_LEN - 1] = '\0'; 
                        corrected = 1;
                        corrected_index = j; 
                        break;
                    } else if (choice == 6) {
                        continue; 
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            }
        }

        strcpy(corrected_command, cmd_parts[0]);
        for (int j = 1; j < i; j++) {
            strcat(corrected_command, " ");
            strcat(corrected_command, cmd_parts[j]);
        }

        if (corrected) {

            int manual_part_index = corrected_index;
            printf("Do you want to open the manual for %s? (yes/no): ", cmd_parts[manual_part_index]);
            char response[4];
            if (fgets(response, sizeof(response), stdin) != NULL) {
                response[strcspn(response, "\n")] = 0; 
                if (strcmp(response, "yes") == 0) {
                    char man_command[MAX_PATH_LEN];
                    if (snprintf(man_command, sizeof(man_command), "man %s | less", cmd_parts[manual_part_index]) >= (int)sizeof(man_command)) {
                        fprintf(stderr, "Command is too long to open manual\n");
                    } else {
                        int result = system(man_command);
                        if (result == -1) {
                            fprintf(stderr, "Failed to execute the command\n");
                        } else if (WEXITSTATUS(result) != 0) {
                            fprintf(stderr, "Command exited with status %d\n", WEXITSTATUS(result));
                        }
                    }
                }
            }
        } else {
            printf("Correct part: %s\n", corrected_command);
        }
    }
}

int main() {
    monitor_commands();
    return 0;
}