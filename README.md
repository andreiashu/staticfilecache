# Static File Cache

Master [![Build Status](https://travis-ci.org/andreiashu/staticfilecache.svg?branch=master)](https://travis-ci.org/andreiashu/staticfilecache)

Drupal Static File Cache (SFC) module helps locking down critical cache Bins/CIDs while still allow for a Memcache/DB/[YourCache] fallback for data caching.

## Goals
SFC was created to ensure a more predictable behaviour of Drupal in high traffic scenarios, where a compromise on flexibility can be made (in terms of one being able to change the system's behaviour straight on the production environment). Because Drupal (Core and Contrib) stores some of its configuration data as cache, this means that if that cache is volatile then the behaviour of the system becomes unpredictable.

SFC enables developer(s) to dump specific cache items on filesystem and that are delivered along with the rest of the code. Think of it like [Features](https://www.drupal.org/project/features) for cache.

## Why would you use SFC?
* you  want to be able to do a cache clear all on your production server without your (popular) website going down; Or without having some pages/paths "magically" dissapearing while cache clear is in progress;
* you are using the [JS module](https://www.drupal.org/project/js) to minimal bootstrap specific paths and sometimes get weird, unexplicable behaviour on other pages on your website?
* you want to ensure consistent / predictable behaviour of your system

## Caveats
Whitelisted caches are never cleared or refreshed (unless you enable these settings. This means that if, for example, you whitelist "cache_bootstrap-hook_info", "cache_bootstrap-system_list", "cache_bootstrap-bootstrap_modules", "cache_bootstrap-module_implements" AND your SFC settings do not allow for updates/deletes then you shouldn't use the modules page for enabling/disabling modules anymore. This is because your system's cache will still represent the original state of enabled modules.

Another caveat is that some modules will try to set cache data that changes depending on the active language being used, without having a CID (Cache ID) that respects that (ie. no language suffic in the CID). If you whitelist these CIDs it will mean that you will get the same data from cache irrespective of your active language.

Because of the caveats listed above, one needs to be careful of their whitelist.

## How SFC works
SFC is a class that implements the "DrupalCacheInterface" Drupal Core interface. It allows one to specify a list of whitelisted (regex supported) Bins/Cache IDs and a fallback cache class. This results in the ability to lock down some (config) caches to a specific state while allowing cache data to be stored in more volatile, memory based cache solutions.

### Settings exposed:
```php
// include the required files for the static file cache and the memcache fallback
$conf['cache_backends'][] = 'sites/all/modules/staticfilecache/DrupalStaticFileCache.inc';
$conf['cache_backends'][] = 'sites/all/modules/memcache/memcache.inc';

// static file cache will handle all the cache_(get/set/clear) calls
$conf['cache_default_class'] = 'DrupalStaticFileCache';
// set the fallback to memcache. If this option is not specified all the cache_* calls that
// do not match a whitelisted cache identifier will be passed to DrupalFakeCache!
$conf['cache_staticfile_fallback'] = 'MemCacheDrupal';

$conf += array(
  // where will you store the cached files
  // most probably you don't want this in the files directory
  // but somewhere in your repository
  'cache_staticfile_cache_dir' => ABSOLUTE_PATH_TO_STATIC_FILE_DIRECTORY,
  // list of whitelisted cache identifiers. Supports regex
  // where there is a match this happens:
  //   - if the file exists in the 'cache_staticfile_cache_dir'
  //     directory return that
  //   - else returns FALSE
  // where there isn't a match then the call is passed to
  // the "cache_staticfile_fallback" class
  'cache_staticfile_whitelist_cids' => array(
    // format is [BIN]-[CID] where the ":" are replaced with "_"
    'cache_bootstrap-module_implements',
    // regex: match CIDs starting with "entity_info" from
    // the Bin "cache"
    '/cache-entity_info_.+/',
  ),
  // defaults to TRUE. if identifier is whitelisted whether SFC
  // should allow cache_get on it
  'cache_staticfile_get_allowed' => TRUE,

  // defaults to FALSE. if identifier is whitelisted whether SFC
  // should try to CREATE a file upon a cache_set call
  'cache_staticfile_add_allowed' => TRUE,

  // defaults to FALSE. if identifier is whitelisted whether SFC
  // should try to UPDATE an existing file upon a cache_set call
  'cache_staticfile_update_allowed' => TRUE,

  // defaults to FALSE. if identifier is whitelisted whether SFC
  // should try to DELETE a file upon a cache_clear call
  'cache_staticfile_delete_allowed' => TRUE,
);
```

## How is SFC different than File Cache module
[File Cache module](https://www.drupal.org/project/filecache) allows one to store whole
Cache Bins on the filesystem. It includes hooks for cron and other functionality that
enables it to behave just like any other cache system (ie. expiry, clear cache, etc).

This is different from SFC because:
* SFC was designed to specifically cache forever: ie. no clear on Cron, or drush cc all;
* SFC has more granularity it it's whitelisting approach: it supports Bins and Cache IDs as well as regex identifiers;
* SFC is meant to be just a Cache Class, not a module in itself;
* SFC allows for a fallback caching layer therefore you can have cache items from the same Bin that can use different caching layers;

Depending on your needs, File Cache module might be more appropriate.

## Credits
This module was created as part of a [Eurostar](http://www.eurostar.com) project.

@TODO
-----

* Use cases (mention minimal Drupal Bootstrap, JS Module)
* Troubleshooting
