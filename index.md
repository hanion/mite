---
global.url = "https://hanion.dev";
global.title = "hanion.dev";
global.description = "recreational programmer";

ADD_PROJECT("project1", "desc1", "/project/project1_url");
ADD_PROJECT("project2", "desc2", "/project/project2_url");

ADD_SOCIAL("github", "https://github.com/user");
ADD_SOCIAL("bluesky", "https://bsky.app/profile/user");
ADD_SOCIAL("linkedin", "https://linkedin.com/user");
---

<? if (global.posts.count > 0) { ?>
## posts

<ul class="post-list">
    <? sort_pages(&global.posts); ?>
    <? for (int i = 0; i < global.posts.count; ++i) { ?>
        <?     SitePage* p = global.posts.items[i]; ?>
        <?     if ((p->date) && *(p->date) == '0') continue; ?>
        <li>
            <time datetime="<? STR(p->date); ?>"><? STR(p->date); ?></time> 
            <a href="<? STR(p->url); ?>"><? STR(p->title); ?></a>
        </li>
        <? } ?>
</ul>
<? } ?>

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
