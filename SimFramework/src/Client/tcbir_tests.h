#ifndef __CLIENT_TRAINING_H
#define __CLIENT_TRAINING_H

#include "untrusted_util.h"
#include <vector>
#include <string>
#include "ImageSearch.h"

using namespace std;

void tcbir_setup(secure_connection* conn, size_t desc_len, unsigned tcbir_nr_clusters, const char* train_technique);
void tcbir_train_client_kmeans(secure_connection* conn, unsigned tcbir_nr_clusters, char* tcbir_train_mode, char* tcbir_centroids_file, const char* tcbir_dataset_dir, feature_extractor desc);
void tcbir_train_iee_kmeans(secure_connection* conn, char* tcbir_train_mode, feature_extractor desc, const vector<string> files);
void tcbir_train_client_lsh(secure_connection* conn, char* tcbir_train_mode, size_t desc_len, unsigned tcbir_nr_clusters);
void tcbir_add_files(secure_connection* conn, feature_extractor desc, const vector<string> files);

#endif
