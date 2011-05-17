--TEST--
Test setting boolean options
--SKIPIF--
<?php
if (!extension_loaded('svm')) die('skip');
?>
--FILE--
<?php
$svm = new svm();

$options = array(Svm::OPT_SHRINKING => true, Svm::OPT_PROBABILITY => false);

$svm->setOptions($options);

var_dump($options[Svm::OPT_SHRINKING]);
var_dump($options[Svm::OPT_PROBABILITY]);

echo "ok\n";
?>
--EXPECT--
bool(true)
bool(false)
ok
