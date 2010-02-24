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
#include "php_ini.h" // needed for 5.2
#include "Zend/zend_exceptions.h"
#include "ext/standard/info.h"

zend_class_entry *php_svm_sc_entry;
zend_class_entry *php_svm_exception_sc_entry;

static zend_object_handlers svm_object_handlers;

#define SVM_MAX_LINE_SIZE 4096
#define SVM_THROW(message, code) \
zend_throw_exception(php_svm_exception_sc_entry, message, (long)code TSRMLS_CC); \
return;

ZEND_DECLARE_MODULE_GLOBALS(svm);

// TODO: Parameter management (add/edit/delete)
// TODO: Retrieve parameters for existing model
// TODO: Training data in an array
// TODO: Test regression
// TODO: Test multilabel
// TODO: Add serialize and wake up support
// TODO: Add appropriate format doccomments

// TODO: Catch the printed data and store for logging
void print_null(const char *s) {}

/* {{{ SVM SVM::__construct();
The constructor
*/
PHP_METHOD(svm, __construct)
{
	php_svm_object *intern;
	intern = zend_object_store_get_object(getThis() TSRMLS_CC);
	
	// Setup the default parameters to match those in libsvm's svm_train
	intern->param.svm_type = NU_SVC; // C_SVC;
	intern->param.kernel_type = RBF; // Gauss in the house
	intern->param.degree = 3;
	intern->param.gamma = 0;         // 1/num_features
	intern->param.coef0 = 0;
	intern->param.nu = 0.5;
	intern->param.cache_size = 100;
	intern->param.C = 1;
	intern->param.eps = 1e-3;
	intern->param.p = 0.1;
	intern->param.shrinking = 1;
	intern->param.probability = 0;
	intern->param.nr_weight = 0;
	intern->param.weight_label = NULL;
	intern->param.weight = NULL;
	intern->cross_validation = 0;
	
	// Redirect the lib svm output
	extern void (*svm_print_string) (const char *);
	svm_print_string = &print_null;

	return;
}

/* {{{ int SVM::save([string filename]);
Save the model to a file for later use, using the libsvm model format. Returns 1 on success.
*/
PHP_METHOD(svm, save) 
{
	php_svm_object *intern;
	char *filename;
	int filename_len;
	int result;
	
	if (zend_parse_parameters_ex(ZEND_PARSE_PARAMS_QUIET,
	                             ZEND_NUM_ARGS() TSRMLS_CC,
	                             "s", &filename, &filename_len) == FAILURE) {
		SVM_THROW("Save requires a filename as a string", 103);
	}
	
	intern = zend_object_store_get_object(getThis() TSRMLS_CC);
	
	result = svm_save_model(filename, intern->model);
	
	if(result == 0) {
		RETURN_BOOL(1);
	} else {
		RETURN_BOOL(0);
	}
}
/* }}} */

/* {{{ int SVM::load([string filename]);
Load a model genenerated by libsvm. Returns 1 on success, 0 on failure. 
*/
PHP_METHOD(svm, load)
{
	php_svm_object *intern;
	char *filename;
	int filename_len;
	int result;
	
	if (zend_parse_parameters_ex(ZEND_PARSE_PARAMS_QUIET,
	                             ZEND_NUM_ARGS() TSRMLS_CC,
	                             "s", &filename, &filename_len) == FAILURE) {
		SVM_THROW("Load requires a filename as a string", 103);
		
	}
	
	intern = zend_object_store_get_object(getThis() TSRMLS_CC);
	intern->model = svm_load_model(filename);
	
	// TODO: Probability support
	//		if(svm_check_probability_model(model)==0)
	//		if(svm_check_probability_model(model)!=0)
	
	if(intern->model == 0) {
		RETURN_BOOL(0);
	} else {
		RETURN_BOOL(1);
	}
}
/* }}} */

/* {{{ double SVM::predict([array data]);
Given an array of data, predict a class label for it using the previously generated model. Returns prediction between -1.0 and +1.0
@throws SVMExceptiopn if model is not available
*/
PHP_METHOD(svm, predict) 
{	
	// TODO: probability stuff
	php_svm_object *intern;
	double predict_label;
	struct svm_node *x;
	int max_nr_attr = 64;
	zval *arr, **data;
	HashTable *arr_hash;
	HashPosition pointer;
	int array_count, i;
	char *endptr;

	// we want an array of data to be passed in
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "a", &arr) == FAILURE) {
	    RETURN_NULL();
	}

	arr_hash = Z_ARRVAL_P(arr);
	array_count = zend_hash_num_elements(arr_hash);
	intern = zend_object_store_get_object(getThis() TSRMLS_CC);
	if(!intern->model) {
		SVM_THROW("No model available to classify with", 106);
	}
	
	// need 1 extra to indicate the end
	x = (struct svm_node *) malloc((array_count + 1) *sizeof(struct svm_node));
	
	i = 0;
	zval temp;
	char *key;
	int key_len;
	long index;
	
	// Loop over the array in the argument and convert into svm_nodes for the prediction
	for(zend_hash_internal_pointer_reset_ex(arr_hash, &pointer);
		zend_hash_get_current_data_ex(arr_hash, (void**) &data, &pointer) == SUCCESS; 
		zend_hash_move_forward_ex(arr_hash, &pointer)) 
	{
		if (zend_hash_get_current_key_ex(arr_hash, &key, &key_len, &index, 0, &pointer) == HASH_KEY_IS_STRING) {
			x[i].index = (int)strtol(key, &endptr, 10);;
		} else {
			x[i].index = index;
		} 
		zval_dtor(&temp);
		temp = **data;
		zval_copy_ctor(&temp);
		convert_to_double(&temp);
		x[i].value = Z_DVAL(temp);
		i++;
	}
	// needed so the predictor knows when to end
	x[i].index = -1;

	/*
	// TODO: This stuff
	if (predict_probability && (svm_type==C_SVC || svm_type==NU_SVC))
	{
		predict_label = svm_predict_probability(model,x,prob_estimates);
		fprintf(output,"%g",predict_label);
		for(j=0;j<nr_class;j++)
			fprintf(output," %g",prob_estimates[j]);
		fprintf(output,"\n");
	}
	*/
	
	predict_label = svm_predict(intern->model, x);
	free(x);

	/*
	// TODO: Prob stuff
	if(predict_probability)
		free(prob_estimates);
		*/	
	RETURN_DOUBLE(predict_label);
}
/* }}} */


/* {{{ int SVM::train([mixed filename]);
Train a SVM based on the SVMLight format data either in a file, or in a previously opened stream. 
@throws SVMException if the data format is incorrect
*/
PHP_METHOD(svm, train)
{
	zval *zstream;
	char *filename;
	int filename_len;
	unsigned char our_stream;
	
	php_svm_object *intern;
	php_stream *stream;

	// TODO: Allow training from an array of data
	// TODO: Allow training from a string containing svmlight formatted data
	if (zend_parse_parameters_ex(ZEND_PARSE_PARAMS_QUIET,
	                             ZEND_NUM_ARGS() TSRMLS_CC,
	                             "s", &filename, &filename_len) == SUCCESS) {
		stream = php_stream_open_wrapper(filename, "r", REPORT_ERRORS, NULL);
		our_stream = 1;
	} else if (zend_parse_parameters_ex(ZEND_PARSE_PARAMS_QUIET,
	                                    ZEND_NUM_ARGS(), "r", &zstream) == SUCCESS) {
	    php_stream_from_zval(stream, &zstream);
		our_stream = 0;
		
	} else {
		return;
	}
	
	intern = zend_object_store_get_object(getThis() TSRMLS_CC);
	
	int elements, max_index, inst_max_index, i, j;
	char *endptr;
	char *idx, *val, *label;
	char *error_msg;
	char str[SVM_MAX_LINE_SIZE];
	
	intern->prob.l = 0;
	elements = 0;
	
	// This just gets the max length in entries and dimensions
	while(!php_stream_eof(stream)) {
		char buf[SVM_MAX_LINE_SIZE];
		if (php_stream_gets(stream, buf, sizeof(buf))) {
			char *p = strtok(buf, " \t");
			while(1) {
				p = strtok(NULL, " \t");
				if(p == NULL || *p == '\n') {
					break;
				}
				++elements;
			}
			++elements;
			intern->prob.l++;
		} else {
		    break;
		}
	}
	php_stream_rewind(stream);
	
	intern->prob.y = (double *)malloc((intern->prob.l)*sizeof(double));
	intern->prob.x = (struct svm_node *)malloc((intern->prob.l)*sizeof(struct svm_node));
	intern->x_space = (struct svm_node *)malloc((elements)*sizeof(struct svm_node));
	max_index = 0;
	j=0;
	
	for(i=0; i<intern->prob.l; i++)	{
		char buf[SVM_MAX_LINE_SIZE];
		inst_max_index = -1; // strtol gives 0 if wrong format, and precomputed kernel has <index> start from 0
		if (php_stream_gets(stream, buf, sizeof(buf))) {
			intern->prob.x[i] = &(intern->x_space[j]);
			label = strtok(buf," \t");
			intern->prob.y[i] = strtod(label,&endptr);
			if(endptr == label) {
				sprintf(error_msg, "Invalid format at line %d", i+1);
				SVM_THROW(error_msg, 101);
			}

			while(1) {
				idx = strtok(NULL,":");
				val = strtok(NULL," \t");

				if(val == NULL) {
					break;
				}
		                        
				errno = 0;
				if(j > elements) {
					SVM_THROW("More data than expected found", 104);
				}
				intern->x_space[j].index = (int) strtol(idx,&endptr,10);
				if(endptr == idx || errno != 0 || *endptr != '\0' || intern->x_space[j].index <= inst_max_index) {
					sprintf(error_msg, "Invalid format at line %d", i+1);
					SVM_THROW(error_msg, 101);
				} else {
					inst_max_index = intern->x_space[j].index;
				}

				errno = 0;
				intern->x_space[j].value = strtod(val,&endptr);
				if(endptr == val || errno != 0 || (*endptr != '\0' && !isspace(*endptr))) {
					sprintf(error_msg, "Invalid format at line %d", i+1);
					SVM_THROW(error_msg, 101);
					
				}

				++j;
			}

			if(inst_max_index > max_index) {
				max_index = inst_max_index;
			}

			intern->x_space[j++].index = -1;
		}
	}

	if(intern->param.gamma == 0 && max_index > 0) {
		intern->param.gamma = 1.0/max_index;
	}
	
	// Validate the parameters aren't mental
	error_msg = svm_check_parameter(&(intern->prob), &(intern->param));
	if(error_msg != NULL) {
		SVM_THROW(error_msg, 102);
	}
	

	
	// TODO: add a setter for cross validaton
	if(intern->cross_validation) {
		// TODO: implement the cross validation stuff
		//do_cross_validation();
	} else {
		// Execute the main training function. This is the science bit. 
		// TODO: Catch the training output that goes straight to stderr
		intern->model = svm_train(&(intern->prob), &(intern->param));
	}

	free(intern->prob.y);
	free(intern->prob.x);
    free(intern->x_space);
	if(our_stream == 1) {
		php_stream_close(stream);
	}
	
	RETURN_BOOL(1);
}
/* }}} */


// TODO: ini variables
PHP_INI_BEGIN()
	STD_PHP_INI_ENTRY("svm.test_bool", "0", PHP_INI_ALL, OnUpdateBool, test, zend_svm_globals, svm_globals)
PHP_INI_END()

// TODO: do we need any globals?
static void php_svm_init_globals(zend_svm_globals *svm_globals)
{
	svm_globals->test = 0;
}

// TODO: Check that we're freeing everything we create
static void php_svm_object_free_storage(void *object TSRMLS_DC)
{
	php_svm_object *intern = (php_svm_object *)object;

	if (!intern) {
		return;
	}
	svm_destroy_model(intern->model);
    svm_destroy_param(&(intern->param));
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

static zend_object_value php_svm_clone_object(zval *this_ptr TSRMLS_DC)
{
	php_svm_object *new_obj = NULL;
	php_svm_object *old_obj = (php_svm_object *) zend_object_store_get_object(this_ptr TSRMLS_CC);
	zend_object_value new_ov = php_svm_object_new_ex(old_obj->zo.ce, &new_obj TSRMLS_CC);
	
	// TODO: copy model across for clone
	// TODO: copy params across for clone

	zend_objects_clone_members(&new_obj->zo, new_ov, &old_obj->zo, Z_OBJ_HANDLE_P(this_ptr) TSRMLS_CC);
	return new_ov;
}

ZEND_BEGIN_ARG_INFO_EX(svm_empty_args, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_train_args, 0, 0, 1)
	ZEND_ARG_INFO(0, problem)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_predict_args, 0, 0, 1)
	ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_file_args, 0, 0, 1)
	ZEND_ARG_INFO(0, filename)
ZEND_END_ARG_INFO()

static function_entry php_svm_class_methods[] =
{
	/* Iterator interface */
	PHP_ME(svm, __construct,	svm_empty_args,	ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
	PHP_ME(svm, train,			svm_train_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svm, save,			svm_file_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svm, load,			svm_file_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svm, predict, 		svm_predict_args, ZEND_ACC_PUBLIC)
	{ NULL, NULL, NULL }
};

PHP_MINIT_FUNCTION(svm)
{
	zend_class_entry ce;
	ZEND_INIT_MODULE_GLOBALS(svm, php_svm_init_globals, NULL);

	REGISTER_INI_ENTRIES();

	memcpy(&svm_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));

	INIT_CLASS_ENTRY(ce, "svm", php_svm_class_methods);
	ce.create_object = php_svm_object_new;
	svm_object_handlers.clone_obj = php_svm_clone_object;
	php_svm_sc_entry = zend_register_internal_class(&ce TSRMLS_CC);

	INIT_CLASS_ENTRY(ce, "svmexception", NULL);
	php_svm_exception_sc_entry = zend_register_internal_class_ex(&ce, zend_exception_get_default(TSRMLS_C), NULL TSRMLS_CC);
	php_svm_exception_sc_entry->ce_flags |= ZEND_ACC_FINAL;

#define SVM_REGISTER_CONST_LONG(const_name, value) \
	zend_declare_class_constant_long(php_svm_sc_entry, const_name, sizeof(const_name)-1, (long)value TSRMLS_CC);	

	// TODO: Do we need any constants?
	SVM_REGISTER_CONST_LONG("CONST_NAME", 1);

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
