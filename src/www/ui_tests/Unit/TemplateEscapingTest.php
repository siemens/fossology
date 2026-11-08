<?php
/*
 SPDX-FileCopyrightText: © 2026 Fossology contributors

 SPDX-License-Identifier: GPL-2.0-only
*/

use Twig\Environment;
use Twig\Loader\ArrayLoader;

/**
 * @class TemplateEscapingTest
 * @brief Autoescape is off, so text users control needs an explicit filter
 */
class TemplateEscapingTest extends \PHPUnit\Framework\TestCase
{
  const ROOT = __DIR__ . '/../../..';

  /**
   * @return array Template, expression that must be escaped
   */
  public static function escapedOutputs()
  {
    $ui = 'www/ui/template/';
    return [
      [$ui . 'ui-clearing-view.html.twig', 'searchQuery'],
      [$ui . 'ui-clearing-view.js.twig', 'searchQuery'],
      ['reuser/ui/template/reusecompare.html.twig', 'searchQuery'],
      ['reuser/ui/template/reusecompare.html.twig', 'src.name'],
      ['reuser/ui/template/reusecompare.html.twig', 'row.upload_file_name'],
      ['reuser/ui/template/reusecompare.html.twig', 'row.reuse_file_name'],
      [$ui . 'reuser.html.twig', 'row.item.fileName'],
      [$ui . 'reuser.html.twig', 'license.shortName'],
      [$ui . 'picker.html.twig', 'selectedPaths[pk]'],
      [$ui . 'multicompare.html.twig', 'col.display_name'],
      ['spasht/ui/template/agent_spasht_search.html.twig', 'uploadName'],
      [$ui . 'admin_bulk_text_export.html.twig', 'user_name'],
      [$ui . 'admin_bulk_text_export.html.twig', 'group_name'],
      [$ui . 'admin_license_candidate-merge.html.twig', 'suggest_text'],
      [$ui . 'admin_license_candidate-merge.html.twig', 'lic.fullname'],
      [$ui . 'admin_license_candidate-merge.html.twig', 'lic.shortname'],
      [$ui . 'ui-browse.html.twig', 'folderName'],
      ['lib/php/UI/template/components/menu.html.twig', 'userName'],
      ['lib/php/UI/template/include/upload.html.twig', 'descriptionInputValue'],
      ['spasht/ui/template/agent_spasht_search.html.twig', 'revisionName'],
      ['spasht/ui/template/agent_spasht_search.html.twig', 'typeName'],
      ['spasht/ui/template/show_definitions.html.twig', 'result.name'],
      ['spasht/ui/template/show_definitions.html.twig', 'result.namespace'],
      ['spasht/ui/template/show_uploads_table.html.twig', 'body.name'],
      ['spasht/ui/template/show_uploads_table.html.twig', 'body.namespace'],
      [$ui . 'about.html.twig', 'latestVersion.tag_name'],
      [$ui . 'admin_license-upload_form.html.twig', 'req_marydone'],
      [$ui . 'admin_license-upload_form.html.twig', 'req_shortname'],
      [$ui . 'admin_license-upload_form.html.twig', 'rf_url'],
      [$ui . 'admin_license-upload_form.html.twig', 'rfId'],
      [$ui . 'browse.html.twig', 'shortname'],
      [$ui . 'file-browse.html.twig', 'shortname'],
      [$ui . 'change_license_modal.html.twig', 'opt.shortname'],
      [$ui . 'popup_license.html.twig', 'url'],
      ['lib/php/UI/template/components/login-form.html.twig', 'referrer'],
      [$ui . 'admin_license_std_comments.html.twig', 'comment.name'],
      [$ui . 'admin_license_std_comments.html.twig', 'comment.comment'],
      [$ui . 'admin_license_acknowledgements.html.twig', 'acknowledgement.name'],
      [$ui . 'admin_license_acknowledgements.html.twig', 'acknowledgement.acknowledgement'],
      [$ui . 'user_edit.html.twig', 'singleToken.name'],
      [$ui . 'user_edit.html.twig', 'singleClient.name'],
      ['lib/php/UI/template/include/macros.html.twig', 'key'],
      ['lib/php/UI/template/include/macros.html.twig', 'disp'],
      [$ui . 'ui-browse.html.twig', 'disp'],
      [$ui . 'ui-search.html.twig', 'uploadObj.getFilename()'],
      ['reuser/ui/template/reusecompare.html.twig', 'lic'],
      ['spasht/ui/template/show_definitions.html.twig', 'result.revision'],
      ['spasht/ui/template/show_uploads_table.html.twig', 'body.provider'],
      ['spasht/ui/template/show_details_table.html.twig', 'result.files'],
      [$ui . 'about.html.twig', 'lastestCommit'],
      ['spasht/ui/template/show_details_table.html.twig', 'result.url'],
      ['spasht/ui/template/agent_spasht_ui_tabs.html.twig', 'row.url'],
      ['spasht/ui/template/agent_spasht_ui_tabs.html.twig', 'row.content'],
      [$ui . 'about.html.twig', 'latestVersion.zipball_url'],
    ];
  }

  /**
   * @test
   * @dataProvider escapedOutputs
   */
  public function testOutputIsEscaped($template, $expression)
  {
    $source = file_get_contents(self::ROOT . '/' . $template);
    $name = preg_quote($expression, '/');
    $this->assertSame(0, preg_match('/\{\{\s*' . $name . '\s*\}\}/', $source),
      "$expression is printed unescaped in $template");
    $this->assertSame(1, preg_match('/\{\{\s*' . $name . '\s*\|\s*e\b/', $source),
      "$expression is missing from $template");
  }

  /**
   * @test Only web URLs become links; anything else is shown as text
   */
  public function testLinksOnlyForWebSchemes()
  {
    $twig = new Environment(new \Twig\Loader\FilesystemLoader([
      self::ROOT . '/www/ui/template', self::ROOT . '/lib/php/UI/template']));
    $twig->addExtension(new \Fossology\Lib\UI\TwigExtensions\TranslateFilter());
    foreach (['javascript:alert(1)', 'JaVaScRiPt:x', 'data:text/html,x'] as $url) {
      $html = $twig->render('popup_license.html.twig', ['url' => $url]);
      $this->assertStringNotContainsString('href="' . $url, $html, $url);
      $this->assertStringContainsString(htmlspecialchars($url), $html, $url);
    }
    $html = $twig->render('popup_license.html.twig', ['url' => 'https://example.org/?a=1&b="2"']);
    $this->assertStringContainsString(
      '<a href="https://example.org/?a=1&amp;b=&quot;2&quot;"', $html);
  }

  /**
   * @test
   * -# The filter must neutralise markup with autoescape off, as configured
   */
  public function testFiltersNeutraliseMarkup()
  {
    $twig = new Environment(new ArrayLoader([
      'html' => '<input value="{{ v|e }}">',
      'js' => 'x = "{{ v|e(\'js\') }}";',
    ]), ['autoescape' => false]);
    $value = '"><b>x</b>';
    $this->assertStringNotContainsString('<b>', $twig->render('html', ['v' => $value]));
    $rendered = $twig->render('js', ['v' => $value]);
    $this->assertStringNotContainsString('"><b>', $rendered);
  }

  /**
   * @return array PHP file, escaped expression that builds HTML from a file name
   */
  public static function phpEscapedOutputs()
  {
    return [
      ['lib/php/common-dir.php', "htmlspecialchars(\$P['ufile_name'])"],
      ['www/ui/user-del.php', "htmlspecialchars(\$row['user_name'])"],
      ['www/ui/async/AjaxBrowse.php', '">" . htmlspecialchars($disp) . "</option>"'],
      ['www/ui/page/AdminGroupDelete.php', 'htmlspecialchars($groupMap[$groupId])'],
      ['www/ui/ui-browse.php', "\$Name = htmlspecialchars(\$Row['ufile_name']);"],
      ['lib/php/common-compare.php', "htmlspecialchars(\$PathElt['ufile_name'])"],
      ['www/ui/admin-folder-size.php', "htmlspecialchars(\$result['upload_filename'])"],
      ['nomos/ui/nomos-diff.php', "htmlspecialchars(\$TreeInfo1['ufile_name'])"],
      ['nomos/ui/nomos-diff.php', "htmlspecialchars(\$TreeInfo2['ufile_name'])"],
      ['buckets/ui/ui-buckets.php', "htmlspecialchars(\$C['ufile_name'])"],
      ['www/ui/async/AjaxShowJobs.php', "htmlspecialchars(\$jobs['upload']['upload_filename'])"],
      ['www/ui/async/AjaxExplorer.php', "\$fileName = htmlspecialchars(\$child['ufile_name']);"],
      ['www/ui/async/AjaxExplorer.php', "\$licenseHtml = htmlspecialchars(\$shortName);"],
      ['www/ui/async/AjaxExplorer.php', "\$licenseEntries = array_map('htmlspecialchars', \$licenseEntries);"],
      ['www/ui/async/AjaxExplorer.php', "\$concludedLicenses[\$projectedId] = htmlspecialchars(\$projectedName);"],
      ['www/ui/async/AjaxExplorer.php', "\$fileDetails[\"fileName\"] = \$child['ufile_name'];"],
    ];
  }

  /**
   * @test
   * @dataProvider phpEscapedOutputs
   * Source check only: it keeps the escaping from being removed.
   */
  public function testPhpOutputIsEscaped($file, $expression)
  {
    $source = file_get_contents(self::ROOT . '/' . $file);
    $this->assertStringContainsString($expression, $source,
      "$file writes a file name into HTML unescaped");
  }
}
