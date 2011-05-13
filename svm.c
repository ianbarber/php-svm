/*
  +----------------------------------------------------------------------+
  | PHP Version 5 / svm                                                  |
  +----------------------------------------------------------------------+
  | Copyright (c) 2010 Ian Barber                                        |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.0 of the PHP license,       |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_0.txt.                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Authors: Ian Barber <ian.barber@gmail.com>                           |
  +----------------------------------------------------------------------+
*/

#include "php_svm.h"
#include "php_svm_internal.h"
#include "php_ini.h" /* needed for 5.2 */
#include "Zend/zend_exceptions.h"
#include "ext/standard/info.h"

static zend_class_entry *php_svm_sc_entry;
static zend_class_entry *php_svm_model_sc_entry;
static zend_class_entry *php_svm_exception_sc_entry;

static zend_object_handlers svm_object_handlers;
static zend_object_handlers svm_model_object_handlers;

#ifndef TRUE
#       define TRUE 1
#       define FALSE 0
#endif


#define SVM_MAX_LINE_SIZE 4096
#define SVM_THROW(message, code) \
		zend_throw_exception(php_svm_exception_sc_entry, message, (long)code TSRMLS_CC); \
		return;

#define SVM_ERROR_MSG_SIZE 512			
#define SVM_THROW_LAST_ERROR(fallback, code) \
		zend_throw_exception(php_svm_exception_sc_entry, (strlen(intern->last_error) ? intern->last_error : fallback), (long)code TSRMLS_CC); \
		memset(intern->last_error, 0, SVM_ERROR_MSG_SIZE); \
		return;			

typedef enum SvmLongAttribute {
	SvmLongAttributeMin = 100,
	phpsvm_svm_type,
	phpsvm_kernel_type,
	phpsvm_degree,
	phpsvm_shrinking,
	phpsvm_probability,
	SvmLongAttributeMax /* Always add before this */
} SvmLongAttribute;

typedef enum SvmDoubleAttribute {
	SvmDoubleAttributeMin = 200,
	phpsvm_gamma,
	phpsvm_nu,
	phpsvm_eps,
	phpsvm_p,
	phpsvm_coef0,
	phpsvm_C,
	phpsvm_cache_size,
	phpsvm_weight,
	SvmDoubleAttributeMax /* Always add before this */
} SvmDoubleAttribute;

/* ---- START HELPER FUNCS ---- */

static void print_null(const char *s) {}

static zend_bool php_svm_set_double_attribute(php_svm_object *intern, SvmDoubleAttribute name, double value) 
{
	if (name >= SvmDoubleAttributeMax) {
		return FALSE;
	}
	
	switch (name) {
		case phpsvm_gamma:
			intern->param.gamma = value;
			break;
		case phpsvm_nu:
			intern->param.nu = value;
			break;
		case phpsvm_eps:
			intern->param.eps = value;
			break;
		case phpsvm_cache_size:
			intern->param.cache_size = value;
			break;
		case phpsvm_p:
			intern->param.p = value;
			break;
		case phpsvm_coef0:
			intern->param.coef0 = value;
			break;
		case phpsvm_C:
			intern->param.C = value;
			break;
		case phpsvm_weight:
			/* Pointer */
			intern->param.weight = &value;
			break;
		default:
			return FALSE;
	}
	
	return TRUE;
}

static zend_bool php_svm_set_long_attribute(php_svm_object *intern, SvmLongAttribute name, long value) 
{
	if (name >= SvmLongAttributeMax) {
		return FALSE;
	}

	switch (name) {
		case phpsvm_svm_type:
			if( value != C_SVC &&
				value != NU_SVC && 
				value != ONE_CLASS && 
				value != EPSILON_SVR && 
				value != NU_SVR ) {
					return FALSE;
			}
			intern->param.svm_type = (int)value;
			break;
		case phpsvm_kernel_type:
			if( value != LINEAR &&
				value != POLY && 
				value != RBF && 
				value != SIGMOID && 
				value != PRECOMPUTED ) {
					return FALSE;
			}
			intern->param.kernel_type = (int)value;
			break;
		case phpsvm_degree:
			intern->param.degree = (int)value;
			break;
		case phpsvm_shrinking:
			intern->param.shrinking = value;
			break;
		case phpsvm_probability:
			intern->param.probability = value;
			break;
		default:
			return FALSE;
	}
	
	return TRUE;
}

/** {{{ zend_bool php_svm_stream_to_array(php_svm_object *intern, php_stream *stream, zval *retval TSRMLS_DC)
	Take a stream containing lines of SVMLight format data and convert them into a PHP array for use by the training
	function.
*/
static zend_bool php_svm_stream_to_array(php_svm_object *intern, php_stream *stream, zval *retval TSRMLS_DC)
{
	while (!php_stream_eof(stream)) {
		char buf[SVM_MAX_LINE_SIZE];
		size_t retlen = 0;
		int line = 1;
		
		/* Read line by line */
		if (php_stream_get_line(stream, buf, SVM_MAX_LINE_SIZE, &retlen)) {
		
			zval *line_array, *pz_label;
			char *label, *ptr, *l = NULL;

			ptr   = buf;
			label = php_strtok_r(ptr, " \t", &l);

			if (!label) {
			    snprintf(intern->last_error, SVM_ERROR_MSG_SIZE, "Incorrect data format on line %d", line);
				return FALSE;
			}
			
			/* The line array */
			MAKE_STD_ZVAL(line_array);
			array_init(line_array);

			/* The label */
			MAKE_STD_ZVAL(pz_label);
			ZVAL_STRING(pz_label, label, 1);
			convert_to_double(pz_label);

			/* Label is the first item in the line array */
			add_next_index_zval(line_array, pz_label);
			
			/* Read rest of the values on the line */
			while (1) {
				char *idx, *value;
				zval *pz_idx, *pz_value;
				
				/* idx:value format */
				idx   = php_strtok_r(NULL, ":", &l);
				value = php_strtok_r(NULL, " \t", &l);
			
				if (!value) {
					break;
				}
				
				/* Make zvals and convert to correct types */
				MAKE_STD_ZVAL(pz_idx);
				ZVAL_STRING(pz_idx, idx, 1);
				convert_to_long(pz_idx);
				
				MAKE_STD_ZVAL(pz_value);
				ZVAL_STRING(pz_value, value, 1);
				convert_to_double(pz_value);
				
				add_index_zval(line_array, Z_LVAL_P(pz_idx), pz_value);
				zval_dtor(pz_idx);
				FREE_ZVAL(pz_idx);
			}
			add_next_index_zval(retval, line_array);
			line++;
		}
	}
	return TRUE;
}
/* }}} */


/* {{{ int _php_count_values(zval *array);
For a an array of arrays, count the number of items in all subarrays. 
*/
static int _php_count_values(zval *array)
{
	zval **ppzval;
	int values = 0;

	for (zend_hash_internal_pointer_reset(Z_ARRVAL_P(array));
		 zend_hash_get_current_data(Z_ARRVAL_P(array), (void **) &ppzval) == SUCCESS;
		 zend_hash_move_forward(Z_ARRVAL_P(array))) {
		
		if (Z_TYPE_PP(ppzval) == IS_ARRAY) {
			values += zend_hash_num_elements(Z_ARRVAL_PP(ppzval));
		}
	}
	return values;
}
/* }}} */

/* {{{ static void php_svm_free_problem(struct svm_problem *problem) {
Free the generated problem.
*/
static void php_svm_free_problem(struct svm_problem *problem) {
	if (problem->x)	{
		efree(problem->x);
	}
		
	if (problem->y) {
		efree(problem->y);
	}
	
	if (problem) {
		efree(problem);
	}
}
/* }}} */

/* {{{ static zend_bool php_svm_read_array(php_svm_object *intern, php_svm_model_object *intern_model, zval *array TSRMLS_DC)
Take a PHP array, and prepare libSVM problem data for training with. 
*/
static struct svm_problem* php_svm_read_array(php_svm_object *intern, php_svm_model_object *intern_model, zval *array TSRMLS_DC)
{
	zval **ppzval;
	
	char *err_msg = NULL;
	char *key;
	char *endptr;
	int i, j = 0, num_labels, elements, max_index = 0, inst_max_index = 0, key_len;
	long index;
	struct svm_problem *problem;
	HashPosition pointer;
	
	/* If reading multiple times make sure that we don't leak */
	if (intern_model->x_space) {
		efree(intern_model->x_space);
		intern_model->x_space = NULL;
	}
	
	if (intern_model->model) {
	    if(LIBSVM_VERSION >= 300) {
	        svm_free_and_destroy_model(&intern_model->model);
	    } else {
	        svm_destroy_model(intern_model->model);
	    }
		
		intern_model->model = NULL;
	}
	
	/* Allocate the problem */
	problem = emalloc(sizeof(struct svm_problem));
	
	/* x and y */
	num_labels = zend_hash_num_elements(HASH_OF(array));
	
	/* Allocate space for the labels */
	problem->y = emalloc(num_labels * sizeof(double));
	
	/* allocate space for x */
	problem->x = emalloc(num_labels * sizeof(struct svm_node *));
	
	/* total number of elements */
	elements = _php_count_values(array);
	
	/* Allocate space: because the original array contains labels the alloc
		should match as we add additional -1 node in the loop */
	intern_model->x_space = emalloc(elements * sizeof(struct svm_node));

	/* How many labels */
	problem->l = num_labels;

	/* Fill the problem */
	for (zend_hash_internal_pointer_reset(Z_ARRVAL_P(array)), i = 0;
		 zend_hash_get_current_data(Z_ARRVAL_P(array), (void **) &ppzval) == SUCCESS;
		 zend_hash_move_forward(Z_ARRVAL_P(array)), i++) {
	
		zval **ppz_label;
	
		if (Z_TYPE_PP(ppzval) != IS_ARRAY) {
			err_msg = "Data format error";
			goto return_error;
		}	
						
		if (zend_hash_num_elements(Z_ARRVAL_PP(ppzval)) < 2) {
			err_msg = "Wrong amount of nodes in the sub-array";
			goto return_error;
		}
		
		problem->x[i] = &(intern_model->x_space[j]);
		
		zend_hash_internal_pointer_reset(Z_ARRVAL_PP(ppzval));
		
		if ((zend_hash_get_current_data(Z_ARRVAL_PP(ppzval), (void **) &ppz_label) == SUCCESS)) {
			
			if (Z_TYPE_PP(ppz_label) != IS_DOUBLE) {
				convert_to_double(*ppz_label);
			}
			problem->y[i] = Z_DVAL_PP(ppz_label);
		} else {
			err_msg = "The sub-array contains only the label. Missing index-value pairs";
			goto return_error;
		}
		
		while (1) {
			zval **ppz_value;

			if ((zend_hash_move_forward(Z_ARRVAL_PP(ppzval)) == SUCCESS) && 
				(zend_hash_get_current_data(Z_ARRVAL_PP(ppzval), (void **) &ppz_value) == SUCCESS)) {						

				if (zend_hash_get_current_key(Z_ARRVAL_PP(ppzval), &key, &index, 0) == HASH_KEY_IS_STRING) {
					intern_model->x_space[j].index = (int) strtol(key, &endptr, 10);
				} else {
					intern_model->x_space[j].index = (int) index;
				}
				
				if (Z_TYPE_PP(ppz_value) != IS_DOUBLE) {
					convert_to_double(*ppz_value);
				}
				intern_model->x_space[j].value = Z_DVAL_PP(ppz_value);
			
				inst_max_index = intern_model->x_space[j].index;
				j++;
			} else {
				break;
			}
		}
		
		intern_model->x_space[j++].index = -1;

		if (inst_max_index > max_index) {
			max_index = inst_max_index;
		}
	}
	
	if (intern->param.gamma == 0 && max_index > 0) {
		intern->param.gamma = 1.0/max_index;
	}
	
	return problem;
	
return_error:
	php_svm_free_problem(problem);
	if (err_msg) {
		snprintf(intern->last_error, SVM_ERROR_MSG_SIZE, err_msg);
	}
		
	return NULL;
}
/* }}} */

/* {{{ static zend_bool php_svm_train(php_svm_object *intern, php_svm_model_object *intern_model, struct svm_problem *problem) 
Train based on a libsvm problem structure
*/
static zend_bool php_svm_train(php_svm_object *intern, php_svm_model_object *intern_model, struct svm_problem *problem) 
{
	const char *err_msg = NULL;
	err_msg = svm_check_parameter(problem, &(intern->param));
	if (err_msg) {
		snprintf(intern->last_error, SVM_ERROR_MSG_SIZE, err_msg);
		return FALSE;
	}

	intern_model->model = svm_train(problem, &(intern->param));

	/* Failure ? */
	if (!intern_model->model) {
		snprintf(intern->last_error, SVM_ERROR_MSG_SIZE, "Failed to train using the data");
		return FALSE;
	}
	
	return TRUE;
}
/* }}} */

/* {{{ static zval* php_svm_get_data_from_param(php_svm_object *intern, zval *zparam)
Take an incoming parameter and convert it into a PHP array of svmlight style data.
*/
static zval* php_svm_get_data_from_param(php_svm_object *intern, zval *zparam TSRMLS_DC) 
{
	zval *data, *return_value;
	zend_bool our_stream = 0;
	zend_bool need_read = 1;
	php_stream *stream = NULL;
	
	switch (Z_TYPE_P(zparam)) {		
		case IS_STRING:
			stream = php_stream_open_wrapper(Z_STRVAL_P(zparam), "r", ENFORCE_SAFE_MODE | REPORT_ERRORS, NULL);
			our_stream = 1;
		break;
		
		case IS_RESOURCE:
			php_stream_from_zval(stream, &zparam);
			our_stream = 0;
		break;
		
		case IS_ARRAY:
			our_stream = 0;
			need_read  = 0;
		break;
		
		default:
			snprintf(intern->last_error, SVM_ERROR_MSG_SIZE, "Incorrect parameter type, expecting string, stream or an array");
			return FALSE;
		break;
	}

	/* If we got stream then read it in */
	if (need_read) {
		if (!stream) {
			snprintf(intern->last_error, SVM_ERROR_MSG_SIZE, "Failed to open the data file");
			return FALSE;
		}
		
		MAKE_STD_ZVAL(data);
		array_init(data);
		
		if (!php_svm_stream_to_array(intern, stream, data TSRMLS_CC)) {
			zval_dtor(data);
			FREE_ZVAL(data);
			if (our_stream) {
				php_stream_close(stream);
			}
			snprintf(intern->last_error, SVM_ERROR_MSG_SIZE, "Failed to read the data");
			return FALSE;
		}
	} else {
		data = zparam;
	}
	
	if (our_stream) {
		php_stream_close(stream);
	}	
	
	return data;
}
/* }}} */

/* ---- END HELPER FUNCS ---- */



/* ---- START SVM ---- */

/* {{{ SVM SVM::__construct();
The constructor
*/
PHP_METHOD(svm, __construct)
{
	php_svm_object *intern;
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "") == FAILURE) {
        SVM_THROW("Invalid parameters passed to constructor", 154);
	}
	
	intern = (php_svm_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	/* Setup the default parameters to match those in libsvm's svm_train */
	php_svm_set_long_attribute(intern, phpsvm_svm_type, C_SVC);
	php_svm_set_long_attribute(intern, phpsvm_kernel_type, RBF);
	php_svm_set_long_attribute(intern, phpsvm_degree, 3);
	php_svm_set_double_attribute(intern, phpsvm_gamma, 0);
	php_svm_set_double_attribute(intern, phpsvm_coef0, 0);
	php_svm_set_double_attribute(intern, phpsvm_nu, 0.5);
	php_svm_set_double_attribute(intern, phpsvm_cache_size, 100.0);
	php_svm_set_double_attribute(intern, phpsvm_C, 1);
	php_svm_set_double_attribute(intern, phpsvm_eps, 1e-3);
	php_svm_set_double_attribute(intern, phpsvm_p, 0.1);
	php_svm_set_long_attribute(intern, phpsvm_shrinking, 1);
	php_svm_set_long_attribute(intern, phpsvm_probability, 0);
	
	return;
}
/* }}} */

/* {{{ array SVM::getOptions();
Get training parameters, in an array. 
*/
PHP_METHOD(svm, getOptions) 
{
	php_svm_object *intern;
	intern = zend_object_store_get_object(getThis() TSRMLS_CC);
	
	array_init(return_value); 
	
	add_index_long(return_value, phpsvm_svm_type, intern->param.svm_type);
	add_index_long(return_value, phpsvm_kernel_type, intern->param.kernel_type);
	add_index_long(return_value, phpsvm_degree, intern->param.degree);
	add_index_long(return_value, phpsvm_coef0, intern->param.shrinking);
	add_index_long(return_value, phpsvm_probability, intern->param.probability);
	
	add_index_long(return_value,  phpsvm_gamma, intern->param.gamma);
	add_index_long(return_value,  phpsvm_coef0, intern->param.coef0);
	add_index_long(return_value,  phpsvm_nu, intern->param.nu);
	add_index_long(return_value,  phpsvm_cache_size, intern->param.cache_size);
	add_index_long(return_value,  phpsvm_C, intern->param.C);
	add_index_long(return_value,  phpsvm_eps, intern->param.eps);
	add_index_long(return_value,  phpsvm_p, intern->param.p);
}
/* }}} */


/* {{{ int SVM::setOptopms(array params);
Takes an array of parameters and sets the training options to match them. 
Only used by the training functions, will not modify an existing model. 
*/
PHP_METHOD(svm, setOptions) 
{
	HashTable *params_ht;
	php_svm_object *intern;
	zval *params, **ppzval;
	char *string_key = NULL;
	ulong num_key;
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "a", &params) == FAILURE) {
		RETURN_FALSE;
	}
	
	params_ht = HASH_OF(params);
	
	if (zend_hash_num_elements(params_ht) == 0) {
		return;
	}
	
	intern = (php_svm_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	for (zend_hash_internal_pointer_reset(params_ht);
		 zend_hash_get_current_data(params_ht, (void **) &ppzval) == SUCCESS;
		 zend_hash_move_forward(params_ht)) {
		
		zval tmp_zval, *tmp_pzval;
		
		if (zend_hash_get_current_key(params_ht, &string_key, &num_key, 1) != HASH_KEY_IS_LONG) {
			continue; /* Ignore the arg (TODO: throw exception?) */
		}
		
		/* Make sure we don't modify the original array */
		tmp_zval = **ppzval;
		zval_copy_ctor(&tmp_zval);
		tmp_pzval = &tmp_zval;
		
		/* Long attribute */
		if (num_key > SvmLongAttributeMin && num_key < SvmLongAttributeMax) {
	
			if (Z_TYPE_P(tmp_pzval) != IS_LONG) {
				convert_to_long(tmp_pzval);
			}
			
			if (!php_svm_set_long_attribute(intern, num_key, Z_LVAL_P(tmp_pzval))) {
				SVM_THROW("Failed to set the attribute", 999);
			}
	
		/* Double attribute */
		} else if (num_key > SvmDoubleAttributeMin && num_key < SvmDoubleAttributeMax) {
			
			if (Z_TYPE_P(tmp_pzval) != IS_DOUBLE) {
				convert_to_double(tmp_pzval);
			}
			
			if (!php_svm_set_double_attribute(intern, num_key, Z_DVAL_P(tmp_pzval))) {
				SVM_THROW("Failed to set the attribute", 999);
			}
			
		} else {
			continue; /* Ignore the arg (TODO: throw exception?) */
		}
		
		tmp_pzval = NULL;
	}
	
	RETURN_TRUE;
}
/* }}} */

/* {{{ double SVM::crossvalidate(mixed string|resource|array, long folds);
Cross validate a the SVM parameters on the training data for tuning parameters. Will attempt to train then classify 
on different segments of the training data (the total number of segments is the folds parameter). The training data
can be supplied as with the train function. For SVM classification, this will we return the correct percentage,
for regression the mean squared error. 
@throws SVMException if the data format is incorrect
*/
PHP_METHOD(svm, crossvalidate)
{
	int i;
	int total_correct = 0;
	long nrfolds;
	double total_error = 0;
	double sumv = 0, sumy = 0, sumvv = 0, sumyy = 0, sumvy = 0;
	struct svm_problem *problem;
	double returnval = 0.0;
	double *target;
	php_svm_object *intern;
	php_svm_model_object *intern_return;
	zval *zparam, *data, *zcount, *tempobj;
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zl", &zparam, &nrfolds) == FAILURE) {
		return;
	}

	intern = (php_svm_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	ALLOC_INIT_ZVAL(tempobj);
	object_init_ex(tempobj, php_svm_model_sc_entry);
	intern_return = (php_svm_model_object *)zend_object_store_get_object(tempobj TSRMLS_CC);
	
	data = php_svm_get_data_from_param(intern, zparam TSRMLS_CC);
	if(!data) {
		SVM_THROW_LAST_ERROR("Could not load data", 234);
	}
	problem = php_svm_read_array(intern, intern_return, data TSRMLS_CC);
	if(!problem) {
		SVM_THROW_LAST_ERROR("Cross validation failed", 1001);
	}
	
 	target = emalloc(problem->l * sizeof(double));
	svm_cross_validation(problem, &(intern->param), nrfolds, target);
	if(intern->param.svm_type == EPSILON_SVR || intern->param.svm_type == NU_SVR) {
		for(i=0;i<problem->l;i++) {
			double y = problem->y[i];
			double v = target[i];
			total_error += (v-y)*(v-y);
			sumv += v;
			sumy += y;
			sumvv += v*v;
			sumyy += y*y;
			sumvy += v*y;
		}
		returnval = (total_error/problem->l); // return total_error divded by number of examples
	} else {
		for(i=0; i<problem->l; i++) {
			if(target[i] == problem->y[i]) {
				++total_correct;
			}
		}
		returnval = 1.0*total_correct/problem->l;
	}
	
	if (data != zparam) {
		zval_dtor(data);
		FREE_ZVAL(data);
	}
	zval_dtor(tempobj);
	FREE_ZVAL(tempobj);
	efree(target);
	php_svm_free_problem(problem);
	
	RETURN_DOUBLE(returnval);
}
/* }}} */

/* {{{ SVMModel SVM::train(mixed string|resource|array, [array classWeights]);
Train a SVM based on the SVMLight format data either in a file, an array, or in a previously opened stream. 
@throws SVMException if the data format is incorrect. Can optionally accept a set of weights that will 
be used to multiply C. Only useful for C_SVC kernels. These should be in the form array(class (int) => weight (float)) 
*/
PHP_METHOD(svm, train)
{
	php_svm_object *intern;
	php_svm_model_object *intern_return;
	struct svm_problem *problem;
	zval *data, *zparam, *retval, *weights, **ppzval;
	HashTable *weights_ht;
	int i;
	char *key;
	long index;
	
	zend_bool status = 0;
	weights = 0;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z|a!", &zparam, &weights) == FAILURE) {
		return;
	}

	intern = (php_svm_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	if(weights && intern->param.svm_type != C_SVC) {
		SVM_THROW("Weights can only be supplied for C_SVC training", 424);
	}
	
	data = php_svm_get_data_from_param(intern, zparam TSRMLS_CC);
	if(!data) {
		SVM_THROW_LAST_ERROR("Could not load data", 234);
	}
	
	if(weights) {
		weights_ht = Z_ARRVAL_P(weights);
		if(zend_hash_num_elements(weights_ht) > 0) {
			intern->param.nr_weight = zend_hash_num_elements(weights_ht);
			intern->param.weight_label = emalloc(intern->param.nr_weight * sizeof(int));
			intern->param.weight = emalloc(intern->param.nr_weight * sizeof(double));
		
			for (zend_hash_internal_pointer_reset(weights_ht), i = 0;
				 zend_hash_get_current_data(weights_ht, (void **) &ppzval) == SUCCESS;
				 zend_hash_move_forward(weights_ht), i++) {

				zval tmp_zval, *tmp_pzval;

				if (zend_hash_get_current_key(weights_ht, &key, &index, 0) == HASH_KEY_IS_LONG) {
					intern->param.weight_label[i] = (int)index;	
				
					/* Make sure we don't modify the original array */
					tmp_zval = **ppzval;
					zval_copy_ctor(&tmp_zval);
					tmp_pzval = &tmp_zval;
					convert_to_double(tmp_pzval);
					intern->param.weight[i] = Z_DVAL_P(tmp_pzval);
					tmp_pzval = NULL;
				}
			}
		}
	} else {
        intern->param.nr_weight = 0;
	}

	/* Return an object */
	object_init_ex(return_value, php_svm_model_sc_entry);
	intern_return = (php_svm_model_object *)zend_object_store_get_object(return_value TSRMLS_CC);

	problem = php_svm_read_array(intern, intern_return, data TSRMLS_CC);
	if(problem != NULL) {
		if (php_svm_train(intern, intern_return, problem)) {
			status = 1;
		} 
		php_svm_free_problem(problem);
	}

	if(weights) {
		efree(intern->param.weight_label);
		efree(intern->param.weight);
	}
	
	if (data != zparam) {
		zval_dtor(data);
		FREE_ZVAL(data);
	}
	
	if (!status) {
		SVM_THROW_LAST_ERROR("Training failed", 1000);
	}
	return;
}
/* }}} */

/* ---- END SVM ---- */

/* ---- START SVMMODEL ---- */

/** {{{ SvmModel::__construct([string filename])
	Constructs an svm model
*/ 
PHP_METHOD(svmmodel, __construct)
{
	php_svm_model_object *intern;
	char *filename = NULL;
	int filename_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|s!", &filename, &filename_len) == FAILURE) {
		SVM_THROW("Invalid parameters passed to constructor", 154);
	}
	
	if (!filename) {
		return;
	}

	intern = (php_svm_model_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	intern->model = svm_load_model(filename);
	
	if (!intern->model) {
		SVM_THROW("Failed to load the model", 1233);	
	}
	
	return;
}
/* }}} */

/** {{{ SvmModel::load(string filename)
	Loads the svm model from a file
*/
PHP_METHOD(svmmodel, load)
{
	php_svm_model_object *intern;
	char *filename = NULL;
	int filename_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &filename, &filename_len) == FAILURE) {
		return;
	}

	intern = (php_svm_model_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	intern->model = svm_load_model(filename);
	
	if (!intern->model) {
		SVM_THROW("Failed to load the model", 1233);
	}
	
	RETURN_TRUE;
}
/* }}} */

/** {{{ SvmModel::save(string filename)
	Saves the svm model to a file
*/
PHP_METHOD(svmmodel, save)
{
	php_svm_model_object *intern;
	char *filename;
	int filename_len;
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &filename, &filename_len) == FAILURE) {
		return;
	}
	
	intern = (php_svm_model_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if (!intern->model) {
		SVM_THROW("The object does not contain a model", 2321);
	}
	
	if (svm_save_model(filename, intern->model) != 0) {
		SVM_THROW("Failed to save the model", 121);
	}
		
	RETURN_TRUE;
}
/* }}} */

/** {{{ SvmModel::predict(array data)
	Predicts based on the model
*/
PHP_METHOD(svmmodel, predict)
{
	php_svm_model_object *intern;
	double predict_label;
	struct svm_node *x;
	int max_nr_attr = 64;
	zval *arr, **data;
	HashTable *arr_hash;
	HashPosition pointer;
	int array_count, i;
	char *endptr;

	/* we want an array of data to be passed in */
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "a", &arr) == FAILURE) {
	    return;
	}

	arr_hash = Z_ARRVAL_P(arr);
	array_count = zend_hash_num_elements(arr_hash);
	intern = (php_svm_model_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	if(!intern->model) {
		SVM_THROW("No model available to classify with", 106);
	}
	
	/* need 1 extra to indicate the end */
	x = safe_emalloc((array_count + 1), sizeof(struct svm_node), 0);
	
	i = 0;
	zval temp;
	char *key;
	uint key_len;
	ulong index;
	
	/* Loop over the array in the argument and convert into svm_nodes for the prediction */
	for(zend_hash_internal_pointer_reset_ex(arr_hash, &pointer);
		zend_hash_get_current_data_ex(arr_hash, (void**) &data, &pointer) == SUCCESS; 
		zend_hash_move_forward_ex(arr_hash, &pointer)) 
	{
		if (zend_hash_get_current_key_ex(arr_hash, &key, &key_len, &index, 0, &pointer) == HASH_KEY_IS_STRING) {
			x[i].index = (int) strtol(key, &endptr, 10);
		} else {
			x[i].index = (int) index;
		} 
		temp = **data;
		zval_copy_ctor(&temp);
		convert_to_double(&temp);
		x[i].value = Z_DVAL(temp);
		zval_dtor(&temp);
		i++;
	}
	/* needed so the predictor knows when to end */
	x[i].index = -1;

	predict_label = svm_predict(intern->model, x);
	efree(x);
	
	RETURN_DOUBLE(predict_label);
}
/* }}} */

/* ---- END SVMMODEL ---- */

static void php_svm_object_free_storage(void *object TSRMLS_DC)
{
	php_svm_object *intern = (php_svm_object *)object;

	if (!intern) {
		return;
	}

	zend_object_std_dtor(&intern->zo TSRMLS_CC);
	efree(intern);
}

static zend_object_value php_svm_object_new_ex(zend_class_entry *class_type, php_svm_object **ptr TSRMLS_DC)
{
	zval *tmp;
	zend_object_value retval;
	php_svm_object *intern;

	/* Allocate memory for the internal structure */
	intern = (php_svm_object *) emalloc(sizeof(php_svm_object));
	memset(&intern->zo, 0, sizeof(zend_object));

	if (ptr) {
		*ptr = intern;
	}
	
	/* Null model by default */
	memset(intern->last_error, 0, 512);

	zend_object_std_init(&intern->zo, class_type TSRMLS_CC);
	zend_hash_copy(intern->zo.properties, &class_type->default_properties, (copy_ctor_func_t) zval_add_ref,(void *) &tmp, sizeof(zval *));

	retval.handle = zend_objects_store_put(intern, NULL, (zend_objects_free_object_storage_t) php_svm_object_free_storage, NULL TSRMLS_CC);
	retval.handlers = (zend_object_handlers *) &svm_object_handlers;
	return retval;
}

static zend_object_value php_svm_object_new(zend_class_entry *class_type TSRMLS_DC)
{
	return php_svm_object_new_ex(class_type, NULL TSRMLS_CC);
}

static void php_svm_model_object_free_storage(void *object TSRMLS_DC)
{
	php_svm_model_object *intern = (php_svm_model_object *)object;

	if (!intern) {
		return;
	}
	
	if (intern->model) {
	    if(LIBSVM_VERSION >= 300) {
	        svm_free_and_destroy_model(&intern->model);
	    } else {
	        svm_destroy_model(intern->model);
	    }
	    
		intern->model = NULL;
	}	
	
	if (intern->x_space) {
		efree(intern->x_space);
		intern->x_space = NULL;
	}

	zend_object_std_dtor(&intern->zo TSRMLS_CC);
	efree(intern);
}

static zend_object_value php_svm_model_object_new_ex(zend_class_entry *class_type, php_svm_model_object **ptr TSRMLS_DC)
{
	zval *tmp;
	zend_object_value retval;
	php_svm_model_object *intern;

	/* Allocate memory for the internal structure */
	intern = (php_svm_model_object *) emalloc(sizeof(php_svm_object));
	memset(&intern->zo, 0, sizeof(zend_object));

	if (ptr) {
		*ptr = intern;
	}
	
	/* Null model by default */
	intern->model = NULL;
	intern->x_space = NULL;
	
	zend_object_std_init(&intern->zo, class_type TSRMLS_CC);
	zend_hash_copy(intern->zo.properties, &class_type->default_properties, (copy_ctor_func_t) zval_add_ref,(void *) &tmp, sizeof(zval *));

	retval.handle = zend_objects_store_put(intern, NULL, (zend_objects_free_object_storage_t) php_svm_model_object_free_storage, NULL TSRMLS_CC);
	retval.handlers = (zend_object_handlers *) &svm_model_object_handlers;
	return retval;
}

static zend_object_value php_svm_model_object_new(zend_class_entry *class_type TSRMLS_DC)
{
	return php_svm_model_object_new_ex(class_type, NULL TSRMLS_CC);
}

ZEND_BEGIN_ARG_INFO_EX(svm_empty_args, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_train_args, 0, 0, 1)
	ZEND_ARG_INFO(0, problem)
	ZEND_ARG_INFO(0, weights)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_crossvalidate_args, 0, 0, 2)
	ZEND_ARG_INFO(0, problem)
	ZEND_ARG_INFO(0, number_of_folds)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_params_args, 0, 0, 1)
	ZEND_ARG_INFO(0, params)
ZEND_END_ARG_INFO()

static function_entry php_svm_class_methods[] =
{
	PHP_ME(svm, __construct,	svm_empty_args,	ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
	PHP_ME(svm, getOptions,		svm_empty_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svm, setOptions,		svm_params_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svm, train,			svm_train_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svm, crossvalidate,	svm_crossvalidate_args,	ZEND_ACC_PUBLIC)
	{ NULL, NULL, NULL }
};

ZEND_BEGIN_ARG_INFO_EX(svm_model_construct_args, 0, 0, 0)
	ZEND_ARG_INFO(0, filename)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_model_predict_args, 0, 0, 1)
	ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_model_file_args, 0, 0, 1)
	ZEND_ARG_INFO(0, filename)
ZEND_END_ARG_INFO()

static function_entry php_svm_model_class_methods[] =
{
	PHP_ME(svmmodel, __construct,	svm_model_construct_args,	ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
	PHP_ME(svmmodel, save,			svm_model_file_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svmmodel, load,			svm_model_file_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svmmodel, predict, 		svm_model_predict_args, ZEND_ACC_PUBLIC)
	{ NULL, NULL, NULL }
};

PHP_MINIT_FUNCTION(svm)
{
	zend_class_entry ce;
	memcpy(&svm_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	memcpy(&svm_model_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));

	INIT_CLASS_ENTRY(ce, "svm", php_svm_class_methods);
	ce.create_object = php_svm_object_new;
	svm_object_handlers.clone_obj = NULL;
	php_svm_sc_entry = zend_register_internal_class(&ce TSRMLS_CC);

	INIT_CLASS_ENTRY(ce, "svmmodel", php_svm_model_class_methods);
	ce.create_object = php_svm_model_object_new;
	svm_model_object_handlers.clone_obj = NULL;
	php_svm_model_sc_entry = zend_register_internal_class(&ce TSRMLS_CC);

	INIT_CLASS_ENTRY(ce, "svmexception", NULL);
	php_svm_exception_sc_entry = zend_register_internal_class_ex(&ce, zend_exception_get_default(TSRMLS_C), NULL TSRMLS_CC);
	php_svm_exception_sc_entry->ce_flags |= ZEND_ACC_FINAL;
	
	/* Redirect the lib svm output */
	svm_set_print_string_function(&print_null);

#define SVM_REGISTER_CONST_LONG(const_name, value) \
	zend_declare_class_constant_long(php_svm_sc_entry, const_name, sizeof(const_name)-1, (long)value TSRMLS_CC);	

	/* SVM types */
	SVM_REGISTER_CONST_LONG("C_SVC", C_SVC);
	SVM_REGISTER_CONST_LONG("NU_SVC", NU_SVC);
	SVM_REGISTER_CONST_LONG("ONE_CLASS", ONE_CLASS);
	SVM_REGISTER_CONST_LONG("EPSILON_SVR", EPSILON_SVR);
	SVM_REGISTER_CONST_LONG("NU_SVR", NU_SVR);
	
	/* Kernel types */
	SVM_REGISTER_CONST_LONG("KERNEL_LINEAR", LINEAR);
	SVM_REGISTER_CONST_LONG("KERNEL_POLY", POLY);
	SVM_REGISTER_CONST_LONG("KERNEL_RBF", RBF);
	SVM_REGISTER_CONST_LONG("KERNEL_SIGMOID", SIGMOID);
	SVM_REGISTER_CONST_LONG("KERNEL_PRECOMPUTED", PRECOMPUTED);
	
	/* Long options (for setOptions) */
	SVM_REGISTER_CONST_LONG("OPT_TYPE", phpsvm_svm_type);
	SVM_REGISTER_CONST_LONG("OPT_KERNEL_TYPE", phpsvm_kernel_type);
	SVM_REGISTER_CONST_LONG("OPT_DEGREE", phpsvm_degree);
	SVM_REGISTER_CONST_LONG("OPT_SHRINKING", phpsvm_shrinking);
	SVM_REGISTER_CONST_LONG("OPT_PROBABILITY", phpsvm_probability);
	
	/* Double options (for setOptions) */
	SVM_REGISTER_CONST_LONG("OPT_GAMMA",  phpsvm_gamma);
	SVM_REGISTER_CONST_LONG("OPT_NU", phpsvm_nu);
	SVM_REGISTER_CONST_LONG("OPT_EPS", phpsvm_eps);
	SVM_REGISTER_CONST_LONG("OPT_P", phpsvm_p);
	SVM_REGISTER_CONST_LONG("OPT_COEF_ZERO", phpsvm_coef0);
	SVM_REGISTER_CONST_LONG("OPT_C", phpsvm_C);
	SVM_REGISTER_CONST_LONG("OPT_CACHE_SIZE", phpsvm_cache_size);
	SVM_REGISTER_CONST_LONG("OPT_WEIGHT", phpsvm_weight);

#undef SVM_REGISTER_CONST_LONG

	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(svm)
{
	UNREGISTER_INI_ENTRIES();
	return SUCCESS;
}

PHP_MINFO_FUNCTION(svm)
{
	php_info_print_table_start();
		php_info_print_table_header(2, "svm extension", "enabled");
		php_info_print_table_row(2, "svm extension version", PHP_SVM_EXTVER);
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}

/* No global functions */
zend_function_entry svm_functions[] = {
	{NULL, NULL, NULL} 
};

zend_module_entry svm_module_entry =
{
	STANDARD_MODULE_HEADER,
	PHP_SVM_EXTNAME,
	svm_functions,				/* Functions */
	PHP_MINIT(svm),				/* MINIT */
	PHP_MSHUTDOWN(svm),			/* MSHUTDOWN */
	NULL,						/* RINIT */
	NULL,						/* RSHUTDOWN */
	PHP_MINFO(svm),				/* MINFO */
	PHP_SVM_EXTVER,				/* version */
	STANDARD_MODULE_PROPERTIES
};


#ifdef COMPILE_DL_SVM
ZEND_GET_MODULE(svm)
#endif /* COMPILE_DL_SVM */
