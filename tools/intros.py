import json
d=json.load(open('tools/mengke.json',encoding='utf8'))
I={
'aixin':('乐美最好的搭档，善良又忠诚，说话爱带一个“啾”字。','百度百科'),
'yinguang':('她会吹长笛，用温柔的魔法治愈大家。','百度百科'),
'xingguang':('她自信又闪亮，最喜欢金色的星星。','百度百科'),
'yueguang':('戴着神奇帽子的小女巫，总是开开心心的。','百度百科'),
'tutu':('她蹦蹦跳跳，遇到困难也不放弃。','fandom 译'),
'hula':('她转起呼啦圈，让大家都动起来。','fandom 译'),
'guangming':('她能让东西发光，把光芒分享给难过的人。','fandom 译'),
'dingdong':('勤劳的小快递员，叮咚，你的包裹到了。','百度百科'),
'liulang':('他用小纸船带走烦恼，其实他就是王子萌可哦。','fandom 译 + 百度百科'),
'dongdong':('她穿着滑轮鞋跑得飞快，喜欢帮助别人。','fandom 译'),
'tietie':('她能把东西变成贴纸，收藏在贴纸本里。','fandom 译'),
'xiongxiong':('她像小熊一样力气超大，也很勇敢。','fandom 译'),
'binbin':('她是纷纷萌可的姐姐，能把东西变大。','fandom 译'),
'fenfen':('她是缤缤萌可的妹妹，能把东西变小。','百度百科'),
'mianyang':('她开了一家洗衣房，把东西洗得干干净净。','fandom 译 + 集名'),
'touming':('她很害羞，害怕的时候会变透明。','fandom 译'),
'gonggong':('她相信大家一起合作最棒。','fandom 译'),
'tiaowu':('她最爱跳舞，还会带着大家一起跳。','fandom 译'),
'huhu':('调皮的小狐狸，会变出好多个自己。','fandom 译 + 百度百科'),
'geyao':('她喜欢音乐，唱起歌来大家都跟着动。','fandom 译'),
'qiaoqiao':('神秘的小怪盗，会悄悄拿走大家的坏心情。','fandom 译'),
'qinqin':('她天真可爱，最喜欢和王子萌可在一起。','fandom 译'),
'wangzi':('北冕座的王子，会骑着独角兽飞翔。','百度百科'),
'jiguang':('守护约定的传奇萌可，勇敢又温柔。','fandom 译'),
}
for m in d: m['intro'],m['intro_src']=I[m['id']]
json.dump(d,open('tools/mengke.json','w',encoding='utf8'),ensure_ascii=False,indent=1)
