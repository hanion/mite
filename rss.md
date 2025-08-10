---
page->layout = NULL;
page->output = "rss.xml";
---
<? sort_pages(&global.posts); ?>
<? STR("\x3c\x3f") ?>
<? STR("xml version=\"1.0\" encoding=\"UTF-8\""); ?>
<? STR("\x3f\x3e\n") ?>
<rss version="2.0" xmlns:atom="http://www.w3.org/2005/Atom">
	<channel>
		<title><? STR(global.title) ?></title>
		<link><? STR(global.url) ?></link>
		<description><? STR(global.description) ?></description>
		<language>en-us</language>
		<lastBuildDate><? STR(format_rfc822(global.posts.items[0]->date)) ?></lastBuildDate>
        <atom:link href="<? STR(global.url) ?>/rss.xml" rel="self" type="application/rss+xml" />
		<? for (int i = 0; i < global.posts.count; i++) { ?>
			<? SitePage* p = global.posts.items[i]; ?>
			<item>
				<title><? STR(p->title) ?></title>
				<link><? STR(global.url) STR(p->url) ?></link>
				<guid isPermaLink="true"><? STR(global.url) STR(p->url) ?></guid>
				<pubDate><? STR(format_rfc822(p->date)) ?></pubDate>
				<description><? STR(p->description) ?></description>
			</item>
		<? } ?>
	</channel>
</rss>


