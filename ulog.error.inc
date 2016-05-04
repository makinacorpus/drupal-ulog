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
 * Logs a PHP error or exception and displays an error page in fatal cases.
 *
 * @param $errors
 * @param $fatal
 */
function _ulog_drupal_log_error($errors, $fatal = FALSE) {

  // Initialize a maintenance theme if the boostrap was not complete.
  // Do it early because drupal_set_message() triggers a drupal_theme_initialize().
  if ($fatal && (drupal_get_bootstrap_phase() != DRUPAL_BOOTSTRAP_FULL)) {
    unset($GLOBALS['theme']);
    if (!defined('MAINTENANCE_MODE')) {
      define('MAINTENANCE_MODE', 'error');
    }
    drupal_maintenance_theme();
  }

  // When running inside the testing framework, we relay the errors
  // to the tested site by the way of HTTP headers.
  $test_info = &$GLOBALS['drupal_test_info'];
  if (!empty($test_info['in_child_site']) && !headers_sent() && (!defined('SIMPLETEST_COLLECT_ERRORS') || SIMPLETEST_COLLECT_ERRORS)) {
    // $number does not use drupal_static as it should not be reset
    // as it uniquely identifies each PHP error.
    static $number = 0;
    foreach ($errors as $error) {
      $assertion = array(
        $error['!message'],
        $error['%type'],
        array(
          'function' => $error['%function'],
          'file' => $error['%file'],
          'line' => $error['%line'],
        ),
      );
      header('X-Drupal-Assertion-' . $number . ': ' . rawurlencode(serialize($assertion)));
      $number++;
    }
  }

  watchdog('php', '%type: !message in %function (line %line of %file).', $errors[0], $errors[0]['severity_level']);

  if ($fatal) {
    drupal_add_http_header('Status', '500 Service unavailable (with message)');
  }

  if (drupal_is_cli()) {
    if ($fatal) {
      // When called from CLI, simply output a plain text message.
      foreach ($errors as $error) {
        print html_entity_decode(strip_tags(t('%type: !message in %function (line %line of %file).', $error))). "\n";
      }
      exit;
    }
  }

  if (isset($_SERVER['HTTP_X_REQUESTED_WITH']) && $_SERVER['HTTP_X_REQUESTED_WITH'] == 'XMLHttpRequest') {
    if ($fatal) {
      foreach ($errors as $error) {
        if (error_displayable($error)) {
          // When called from JavaScript, simply output the error message.
          print t('%type: !message in %function (line %line of %file).', $error);
        }
      }
    }
  }
  else {
    // Display the message if the current error reporting level allows this type
    // of message to be displayed, and unconditionnaly in update.php.
    foreach ($errors as $error) {
      if (error_displayable($error)) {
        $class = 'error';

        // If error type is 'User notice' then treat it as debug information
        // instead of an error message, see dd().
        if ($error['%type'] == 'User notice') {
          $error['%type'] = 'Debug';
          $class = 'status';
        }

        drupal_set_message(t('%type: !message in %function (line %line of %file).', $error), $class);
      }
    }

    if ($fatal) {
      drupal_set_title(t('Error'));
      // We fallback to a maintenance page at this point, because the page generation
      // itself can generate errors.
      print theme('maintenance_page', array('content' => t('The website encountered an unexpected error. Please try again later.')));
    }
  }
}

/**
 * @see _drupal_error_handler().
 * @see _drupal_error_handler_real().
 */
function _ulog_error_handler($error_level, $message, $filename, $line, $context, $getTrace = true) {
  $reporting = error_reporting();
  // Error reporting is set to 0 when the call was prefixed by @
  if ($reporting !== 0 && (isset($GLOBALS['user']) && 1 == $GLOBALS['user']->uid || ($error_level & $reporting))) {
    require_once DRUPAL_ROOT . '/includes/errors.inc';
    $types = drupal_error_levels();
    list($severity_msg, $severity_level) = $types[$error_level];

    if ($getTrace) {
      $i = 0;
      $dtrace = debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS);
      $strace = [];
      //array_shift($dtrace); // This closure.
      foreach ($dtrace as $caller) {
        if (isset($caller['file'])) {
          $strace[$i] = "#$i {$caller['file']}({$caller['line']}): {$caller['function']}()";
        } else {
          $strace[$i] = "#$i [internal function]: {$caller['function']}()";
        }
        ++$i;
      }
    } else {
      $strace = ["#0 {$filename}($line)"];
    }

    if (!function_exists('filter_xss_admin')) {
      require_once DRUPAL_ROOT . '/includes/common.inc';
    }

    // We treat recoverable errors as fatal.
    _ulog_drupal_log_error([[
      '%type'           => isset($types[$error_level]) ? $severity_msg : 'Unknown error',
      '!message'        => '<strong>' . nl2br(filter_xss_admin($message) . "</strong>\n" . implode("\n", $strace)),
      '%function'       => $caller['function'],
      '%file'           => empty($caller['file']) ? '' : $caller['file'],
      '%line'           => empty($caller['line']) ? '' : $caller['line'],
      'severity_level'  => $severity_level,
    ]], $error_level == E_RECOVERABLE_ERROR || $error_level == E_ERROR || $error_level == E_COMPILE_ERROR);
  }
}

// Override Drupal error handler.
set_error_handler('_ulog_error_handler');

/**
 * @see _drupal_exception_handler().
 * @see _drupal_exception_handler_real().
 */
set_exception_handler(function (Exception $e) {
  require_once DRUPAL_ROOT . '/includes/errors.inc';
  try {
    $errors = [];
    $current = $e;
    do {
      $errors[] = [
        '%type'           => get_class($e),
        '!message'        => '<strong>' . nl2br(check_plain($current->getMessage()) . "</strong>\n" . $current->getTraceAsString()),
        '%function'       => $current->getCode(),
        '%file'           => $current->getFile(),
        '%line'           => $current->getLine(),
        'severity_level'  => WATCHDOG_CRITICAL,
      ];
    } while ($current = $current->getPrevious());
    _ulog_drupal_log_error($errors, true);
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

// https://stackoverflow.com/questions/277224/how-do-i-catch-a-php-fatal-error
register_shutdown_function(function () {
  $error = error_get_last();
  if ($error && E_ERROR === $error['type']) {
    _ulog_error_handler($error['type'], $error['message'], $error['file'], $error['line'], null, false);
  }
});
