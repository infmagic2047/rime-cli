#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <json.h>
#include <rime_api.h>

static volatile sig_atomic_t done = 0;

static void signal_handler(int sig);
static char *get_xdg_data_home();
static char *get_user_data_dir();
static bool get_next_key(int *keysym, int *modifiers);
static char *get_output_json(RimeApi *api, RimeSessionId session_id);

static void signal_handler(int sig)
{
    done = 1;
}

static char *get_xdg_data_home()
{
    const char *xdg_data_home = getenv("XDG_DATA_HOME");
    if (xdg_data_home) {
        return strdup(xdg_data_home);
    } else {
        const char *home = getenv("HOME");
        char *path = malloc(strlen(home) + strlen("/.local/share") + 1);
        strcpy(path, home);
        strcat(path, "/.local/share");
        return path;
    }
}

static char *get_user_data_dir()
{
    char *xdg_data_home = get_xdg_data_home();
    char *path = malloc(strlen(xdg_data_home) + strlen("/" PROJECT_NAME) + 1);
    strcpy(path, xdg_data_home);
    strcat(path, "/" PROJECT_NAME);
    free(xdg_data_home);
    return path;
}

static bool get_next_key(int *keysym, int *modifiers)
{
    char str[1024];
    if (!fgets(str, 1024, stdin)) {
        return false;
    }
    json_object *root = json_tokener_parse(str);
    json_object *keysym_obj;
    json_object *modifiers_obj;
    bool ok = true;
    if (!json_object_object_get_ex(root, "keysym", &keysym_obj) || json_object_get_type(keysym_obj) != json_type_int) {
        ok = false;
    }
    if (!json_object_object_get_ex(root, "modifiers", &modifiers_obj) || json_object_get_type(modifiers_obj) != json_type_int) {
        ok = false;
    }
    if (ok) {
        *keysym = json_object_get_int(keysym_obj);
        *modifiers = json_object_get_int(modifiers_obj);
    } else {
        fprintf(stderr, "Invalid json input\n");
        *keysym = 0;
        *modifiers = 0;
    }
    json_object_put(root);
    return true;
}

static char *get_output_json(RimeApi *api, RimeSessionId session_id)
{
    json_object *root = json_object_new_object();

    json_object *commit_json = NULL;
    RIME_STRUCT(RimeCommit, commit);
    if (api->get_commit(session_id, &commit)) {
        commit_json = json_object_new_object();
        if (commit.text) {
            json_object_object_add(commit_json, "text", json_object_new_string(commit.text));
        } else {
            json_object_object_add(commit_json, "text", NULL);
        }
        api->free_commit(&commit);
    }
    json_object_object_add(root, "commit", commit_json);

    json_object *composition_json = NULL;
    json_object *menu_json = NULL;
    RIME_STRUCT(RimeContext, context);
    if (api->get_context(session_id, &context)) {
        if (context.composition.preedit) {
            composition_json = json_object_new_object();
            json_object_object_add(composition_json, "preedit", json_object_new_string(context.composition.preedit));
        }
        if (context.menu.candidates) {
            menu_json = json_object_new_object();
            json_object *candidates_json = json_object_new_array();
            const char *select_keys = context.menu.select_keys ? context.menu.select_keys : "1234567890";
            int i;
            for (i = 0; i < context.menu.num_candidates; i++) {
                json_object *candidate = json_object_new_object();
                json_object_object_add(candidate, "text", json_object_new_string(context.menu.candidates[i].text));
                if (context.menu.candidates[i].comment) {
                    json_object_object_add(candidate, "comment", json_object_new_string(context.menu.candidates[i].comment));
                } else {
                    json_object_object_add(candidate, "comment", NULL);
                }
                if (i < strlen(select_keys)) {
                    char s[2];
                    s[0] = select_keys[i];
                    s[1] = '\0';
                    json_object_object_add(candidate, "label", json_object_new_string(s));
                } else {
                    json_object_object_add(candidate, "label", NULL);
                }
                json_object_array_add(candidates_json, candidate);
            }
            json_object_object_add(menu_json, "candidates", candidates_json);
        }
        api->free_context(&context);
    }
    json_object_object_add(root, "composition", composition_json);
    json_object_object_add(root, "menu", menu_json);

    char *str = strdup(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
    json_object_put(root);
    return str;
}

int main()
{
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = signal_handler;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);

    RimeApi *api = rime_get_api();

    if (!RIME_API_AVAILABLE(api, setup) ||
            !RIME_API_AVAILABLE(api, initialize) ||
            !RIME_API_AVAILABLE(api, finalize) ||
            !RIME_API_AVAILABLE(api, start_maintenance) ||
            !RIME_API_AVAILABLE(api, create_session) ||
            !RIME_API_AVAILABLE(api, destroy_session) ||
            !RIME_API_AVAILABLE(api, find_session) ||
            !RIME_API_AVAILABLE(api, process_key) ||
            !RIME_API_AVAILABLE(api, get_commit) ||
            !RIME_API_AVAILABLE(api, free_commit) ||
            !RIME_API_AVAILABLE(api, get_context) ||
            !RIME_API_AVAILABLE(api, free_context)) {
        fprintf(stderr, "Incompatible rime API\n");
        return 1;
    }

    char *user_data_dir = get_user_data_dir();
    RIME_STRUCT(RimeTraits, rime_traits);
    rime_traits.shared_data_dir = RIME_SHARED_DATA_DIR;
    rime_traits.user_data_dir = user_data_dir;
    rime_traits.distribution_name = "Rime";
    rime_traits.distribution_code_name = PROJECT_NAME;
    rime_traits.distribution_version = PROJECT_VERSION;
    rime_traits.app_name = "rime." PROJECT_NAME;
    api->setup(&rime_traits);
    api->initialize(&rime_traits);
    free(user_data_dir);
    api->start_maintenance(false);
    RimeSessionId session_id = api->create_session();

    while (!done) {
        int keysym;
        int modifiers;
        if (!get_next_key(&keysym, &modifiers)) {
            break;
        }
        if (!api->find_session(session_id)) {
            session_id = api->create_session();
        }
        int status = api->process_key(session_id, keysym, modifiers);
        if (!status) {
            printf("null\n");
        } else {
            char *outstr = get_output_json(api, session_id);
            printf("%s\n", outstr);
            free(outstr);
        }
        fflush(stdout);
    }

    api->destroy_session(session_id);
    api->finalize();
    return 0;
}
