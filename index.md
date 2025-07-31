---
global.url = "https://hanion.dev";
global.title = "hanion.dev";
global.description = "recreational programmer";
---
## posts

<ul class="post-list">
<? for (int i = 0; i < global.pages.count; ++i) {
    SitePage* p = &global.pages.items[i];
    if ((p->date) && *(p->date) == '0') continue; ?>

<li>
<time datetime="<? STR(p->date); ?>"><? STR(p->date); ?></time> 
<a href="<? STR(p->url); ?>"><? STR(p->title); ?></a>
</li>

<? } ?>
</ul>

### <? STR("c in md") ?>

<ul>
<? for (int i = 1; i < 6; ++i) { ?>

<? if (i == 3) { ?>
<li><? STR(global.title) ?></li>
<? } else { ?>
<li><? INT(i) ?></li>
<? } ?>

<? } ?>
</ul>

## usage
```sh
$ cc -o mite mite.c && ./mite
```
