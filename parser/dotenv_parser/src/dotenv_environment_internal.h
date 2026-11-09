#ifndef DOTENV_ENVIRONMENT_INTERNAL_H
#define DOTENV_ENVIRONMENT_INTERNAL_H

/* Internal process-environment boundary shared by DotEnv and the public parser wrapper. */
int dotenv_environment_get_copy(const char *name, char **value_out);
int dotenv_environment_set(const char *name, const char *value, int overwrite);

#endif
