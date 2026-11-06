#ifndef PB_COMMANDS_H
#define PB_COMMANDS_H

typedef int (*pb_command_fn)(int argc, char **argv);

typedef struct pb_command {
    const char *name;
    const char *usage;
    const char *summary;
    pb_command_fn run;
} pb_command_t;

int pb_cmd_list(int argc, char **argv);
int pb_cmd_scan(int argc, char **argv);
int pb_cmd_probe(int argc, char **argv);
int pb_cmd_learn(int argc, char **argv);
int pb_cmd_run(int argc, char **argv);
int pb_cmd_monitor(int argc, char **argv);
int pb_cmd_rumble(int argc, char **argv);
int pb_cmd_record(int argc, char **argv);
int pb_cmd_replay(int argc, char **argv);
int pb_cmd_status(int argc, char **argv);
int pb_cmd_release(int argc, char **argv);
int pb_cmd_stop(int argc, char **argv);
int pb_cmd_profiles(int argc, char **argv);
int pb_cmd_backends(int argc, char **argv);

#endif
