--TEST--
Test some basic info functions
--SKIPIF--
<?php
if (!extension_loaded('svm')) die('skip');
?>
--FILE--
<?php
$svm = new svm();
$svm->setOptions(array(
	SVM::OPT_TYPE => SVM::C_SVC,
	SVM::OPT_KERNEL_TYPE => SVM::KERNEL_LINEAR,
	SVM::OPT_P => 0.1,  // epsilon 0.1
	SVM::OPT_PROBABILITY => 1
));
$model = $svm->train(dirname(__FILE__) . '/abalone.scale');

if($model) {
	if($model->getSvmType() == SVM::C_SVC) {
	    echo "ok\n";
	}
    echo $model->getNrClass(), "\n";
    echo count($model->getLabels()), "\n";
} else {
	echo "loading failed";
}

$data = array(
    "0.1" => array(1 => 1, 2 => 1, 3 => 1),
    "2" => array(1 => 20, 2 => 20, 3 => 20),
    "0.5" => array(1 => 5, 2 => 5, 3 => 5),
);
$svm = new SVM();
$svm->setOptions(array(
    SVM::OPT_TYPE => SVM::EPSILON_SVR,
    SVM::OPT_PROBABILITY => 1
));
$model = $svm->train($data);
echo $model->getSvrProbability() > 11 ? "ok\n" : "fail\n";
var_dump($model->checkProbabilityModel());

?>
--EXPECT--
ok
28
28
ok
bool(true)