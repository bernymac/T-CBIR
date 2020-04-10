#include "Client.h"

#include "untrusted_util.h"
#include "definitions.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sodium.h>
#include <random>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <libconfig.h>

#include "ImageSearch.h"
#include "tcbir_tests.h"
#include "util.h"

using namespace std;

typedef struct configs {
    int use_images = 0;

    unsigned tcbir_nr_docs = 0, tcbir_nr_queries = 0;
    int tcbir_desc_sift = 1, tcbir_dataset_rec = 0;
    char* tcbir_train_mode, *tcbir_train_technique, *tcbir_add_mode, *tcbir_search_mode, *tcbir_clusters_file, *tcbir_dataset_dir, *tcbir_results_file;
    unsigned tcbir_descriptor_threshold, tcbir_nr_clusters;

} configs;

void separated_tests(const configs* const settings, secure_connection* conn) {
    struct timeval start, end;

    ///////////////////////////////
    if(settings->use_images) {
        // image descriptor parameters
        feature_extractor desc(settings->tcbir_desc_sift, settings->tcbir_descriptor_threshold);

        vector<string> files_add, files_search;
        if(settings->tcbir_dataset_rec) {
            files_add = list_img_files_rec(!settings->tcbir_nr_docs ? -1 : settings->tcbir_nr_docs, settings->tcbir_dataset_dir);
            files_search = list_img_files_rec(-1, settings->tcbir_dataset_dir);
        } else {
            files_add = list_img_files(!settings->tcbir_nr_docs ? -1 : settings->tcbir_nr_docs, settings->tcbir_dataset_dir);
            files_search = list_img_files_rec(-1, settings->tcbir_dataset_dir);
        }

        // init iee and server
        gettimeofday(&start, NULL);
        tcbir_setup(conn, desc.get_desc_len(), settings->tcbir_nr_clusters, settings->tcbir_train_technique);
        gettimeofday(&end, NULL);
        printf("-- T-CBIR setup: %lf ms --\n", untrusted_util::time_elapsed_ms(start, end));

        reset_bytes();

        // train
        gettimeofday(&start, NULL);
        if(!strcmp(settings->tcbir_train_technique, "client_kmeans"))
            tcbir_train_client_kmeans(conn, settings->tcbir_nr_clusters, settings->tcbir_train_mode, settings->tcbir_clusters_file, settings->tcbir_dataset_dir, desc);
        else if(!strcmp(settings->tcbir_train_technique, "iee_kmeans"))
            tcbir_train_iee_kmeans(conn, settings->tcbir_train_mode, desc, files_add);
        else if(!strcmp(settings->tcbir_train_technique, "lsh"))
            tcbir_train_client_lsh(conn, settings->tcbir_train_mode, desc.get_desc_len(), settings->tcbir_nr_clusters);

        gettimeofday(&end, NULL);
        printf("-- T-CBIR train: %lf ms %s %s--\n", untrusted_util::time_elapsed_ms(start, end), settings->tcbir_train_technique, settings->tcbir_train_mode);
        print_bytes("train_tcbir");
        reset_bytes();

        // add images to repository
        if(!strcmp(settings->tcbir_add_mode, "normal")) {
            tcbir_add_files(conn, desc, files_add);
        } else if(!strcmp(settings->tcbir_add_mode, "load")) {
            // tell iee to load images from disc
            unsigned char op = OP_IEE_READ_MAP;
            //iee_comm(conn, &op, 1);
            printf("unsupported mode\n");
            exit(1);
        } else {
            printf("Add mode error\n");
            exit(1);
        }

        print_bytes("add_imgs_tcbir");
        reset_bytes();

        /*if(!strcmp(settings->tcbir_add_mode, "normal")) {
            // send persist storage message to iee, useful for debugging and testing
            unsigned char op = OP_IEE_WRITE_MAP;
            iee_comm(conn, &op, 1);
        }*/

        // search
        if(settings->tcbir_nr_queries > 0) {
            // flickr
            search_flickr(conn, desc, files_search, settings->tcbir_nr_queries);
        } else {
            // search test (inria)
            const int dbg_limit = -1;
            search_test(conn, desc, settings->tcbir_results_file, dbg_limit);
        }

        print_bytes("search_tcbir");
        reset_bytes();

        // dump benchmark results
        size_t in_len;
        uint8_t* in;
        dump_bench(&in, &in_len);
        iee_comm(conn, in, in_len);
        free(in);

        // clear
        clear(&in, &in_len);
        iee_comm(conn, in, in_len);
        free(in);
    }
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    setvbuf(stderr, NULL, _IONBF, BUFSIZ);

    const int server_port = IEE_PORT;

    configs program_configs;
    config_t cfg;
    config_init(&cfg);

    if(!config_read_file(&cfg, "../tcbir.cfg")) {
        fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg), config_error_line(&cfg), config_error_text(&cfg));
        config_destroy(&cfg);
        exit(1);
    }

    // addresses
    char* server_name;
    config_lookup_string(&cfg, "iee_hostname", (const char**)&server_name);

    config_lookup_int(&cfg, "use_images", &program_configs.use_images);

    config_lookup_int(&cfg, "tcbir.nr_docs", (int*)&program_configs.tcbir_nr_docs);
    config_lookup_int(&cfg, "tcbir.nr_queries", (int*)&program_configs.tcbir_nr_queries);
    config_lookup_int(&cfg, "tcbir.desc_sift", &program_configs.tcbir_desc_sift);
    config_lookup_string(&cfg, "tcbir.train_technique", (const char**)&program_configs.tcbir_train_technique);
    config_lookup_string(&cfg, "tcbir.train_mode", (const char**)&program_configs.tcbir_train_mode);
    config_lookup_string(&cfg, "tcbir.add_mode", (const char**)&program_configs.tcbir_add_mode);
    config_lookup_string(&cfg, "tcbir.search_mode", (const char**)&program_configs.tcbir_search_mode);
    config_lookup_string(&cfg, "tcbir.dataset_dir", (const char**)&program_configs.tcbir_dataset_dir);
    config_lookup_int(&cfg, "tcbir.dataset_rec", (int*)&program_configs.tcbir_dataset_rec);
    config_lookup_int(&cfg, "tcbir.descriptor_threshold", (int*)&program_configs.tcbir_descriptor_threshold);
    config_lookup_int(&cfg, "tcbir.nr_clusters", (int*)&program_configs.tcbir_nr_clusters);


    char* clusters_file_dir, *results_file_dir;
    config_lookup_string(&cfg, "tcbir.clusters_file_dir", (const char**)&clusters_file_dir);
    config_lookup_string(&cfg, "tcbir.results_file_dir", (const char**)&results_file_dir);

    const char* desc_str;
    if(program_configs.tcbir_desc_sift)
        desc_str = "sift";
    else
        desc_str = "surf";

    char method_char = 'x';
    if(!strcmp(program_configs.tcbir_train_technique, "client_kmeans"))
        method_char = 'c';
    else if(!strcmp(program_configs.tcbir_train_technique, "iee_kmeans"))
        method_char = 'i';
    else if(!strcmp(program_configs.tcbir_train_technique, "lsh"))
        method_char = 'l';

    if(config_lookup_string(&cfg, "tcbir.clusters_file_override", (const char**)&program_configs.tcbir_clusters_file)) {
        printf("clusters file override detected\n");
    } else {
        printf("using default clusters file\n");
        program_configs.tcbir_clusters_file = (char*)malloc(strlen(clusters_file_dir) + 27);
        sprintf(program_configs.tcbir_clusters_file, "%s/centroids_k%04d_%s_%04d", clusters_file_dir, program_configs.tcbir_nr_clusters, desc_str, program_configs.tcbir_descriptor_threshold);
    }

    if(!strcmp(program_configs.tcbir_train_mode, "load"))
        printf("loading clusters %s\n", program_configs.tcbir_clusters_file);

    program_configs.tcbir_results_file = (char*)malloc(strlen(results_file_dir) + 31);
    sprintf(program_configs.tcbir_results_file, "%s/results_k%04d_%s_%04d_%c.dat", results_file_dir, program_configs.tcbir_nr_clusters, desc_str, program_configs.tcbir_descriptor_threshold, method_char);

    if(!strcmp(program_configs.tcbir_train_mode, "train") && !strcmp(program_configs.tcbir_train_technique, "client_kmeans") && (access(program_configs.tcbir_clusters_file, F_OK) != -1)) {
        printf("Clusters file already exists! Is training really needed?\n");
        exit(1);
    }

    // parse terminal arguments
    int c;
    while ((c = getopt(argc, argv, "hk:b:")) != -1) {
        switch (c) {
            case 'k':
                program_configs.tcbir_nr_clusters = (unsigned)std::stoi(optarg);
                break;
            case 'b':
                program_configs.tcbir_nr_docs = (unsigned)std::stoi(optarg);
                break;
            case 'h':
                printf("Usage: ./Client [-b nr_docs] [-k nr_clusters]\n");
                exit(0);
            case '?':
                if (optopt == 'c')
                    fprintf(stderr, "-%c requires an argument.\n", optopt);
                else if (isprint(optopt))
                    fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
                exit(1);
            default:
                exit(-1);
        }
    }

	if(program_configs.use_images)
        printf("T-CBIR: train %s; search %s\n", program_configs.tcbir_train_mode, program_configs.tcbir_search_mode);
	else
		printf("T-CBIR: disabled\n");

    // init mbedtls
    secure_connection* conn;
    untrusted_util::init_secure_connection(&conn, server_name, server_port);


    separated_tests(&program_configs, conn);

    // close ssl connection
    untrusted_util::close_secure_connection(conn);

    return 0;
}

