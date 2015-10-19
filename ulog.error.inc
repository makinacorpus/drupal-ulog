<?php
/**
 * Include this file throught the cache_backend files variables in the
 * settings.php file in order to have a working error handler the sooner
 * you can have it.
 */

/**
 * Just a stupid define.
 *
 * @see ulog_boot().
 */
define('LO_ERROR_LOADED', true);

/**
 * PHP <5.3.6 compat.
 */
if (!defined('DEBUG_BACKTRACE_IGNORE_ARGS')) {
  define('DEBUG_BACKTRACE_IGNORE_ARGS', false);
}

/**
 * @see _drupal_error_handler().
 * @see _drupal_error_handler_real().
 */
set_error_handler(function ($error_level, $message, $filename, $line, $context) {
  $reporting = error_reporting();
  // Error reporting is set to 0 when the call was prefixed by @
  if ($reporting !== 0 && (isset($GLOBALS['user']) && 1 == $GLOBALS['user']->uid || ($error_level & $reporting))) {
    require_once DRUPAL_ROOT . '/includes/errors.inc';
    $types = drupal_error_levels();
    list($severity_msg, $severity_level) = $types[$error_level];

    $i = 0;
    $dtrace = debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS);
    $strace = array();
    //array_shift($dtrace); // This closure.
    foreach ($dtrace as $caller) {
      if (isset($caller['file'])) {
        $strace[$i] = "#$i {$caller['file']}({$caller['line']}): {$caller['function']}()";
      } else {
        $strace[$i] = "#$i [internal function]: {$caller['function']}()";
      }
      ++$i;
    }

    if (!function_exists('filter_xss_admin')) {
      require_once DRUPAL_ROOT . '/includes/common.inc';
    }

    // We treat recoverable errors as fatal.
    _drupal_log_error(array(
      '%type' => isset($types[$error_level]) ? $severity_msg : 'Unknown error',
      // The standard PHP error handler considers that the error messages
      // are HTML. We mimick this behavior here.
      '!message' => nl2br(filter_xss_admin($message) . "\n" . implode("\n", $strace)),
      '%function' => $caller['function'],
      '%file' => empty($caller['file']) ? '' : $caller['file'],
      '%line' => empty($caller['line']) ? '' : $caller['line'],
      'severity_level' => $severity_level,
    ), $error_level == E_RECOVERABLE_ERROR);
  }
});

/**
 * @see _drupal_exception_handler().
 * @see _drupal_exception_handler_real().
 */
set_exception_handler(function (Exception $e) {
  require_once DRUPAL_ROOT . '/includes/errors.inc';
  try {
    _drupal_log_error(array(
      '%type' => get_class($e),
      '!message' => nl2br(check_plain($e->getMessage()) . "\n" . $e->getTraceAsString()),
      '%function' => $e->getCode(),
      '%file' => $e->getFile(),
      '%line' => $e->getLine(),
      'severity_level' => WATCHDOG_ERROR,
    ), true);
  }
  catch (Exception $e2) {
    // Another uncaught exception was thrown while handling the first one.
    // If we are displaying errors, then do so with no possibility of a further uncaught exception being thrown.
    if (error_displayable()) {
      print '<h1>Additional uncaught exception thrown while handling exception.</h1>';
      print '<h2>Original</h2><p>' . $e->getMessage() . '</p><pre>' . $e->getTraceAsString() . '</pre>';
      print '<h2>Additional</h2><p>' . $e2->getMessage() . '</p><pre>' . $e2->getTraceAsString() . '</pre>';
    }
  }
});
