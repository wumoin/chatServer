-- 0000_set_updated_at_function.sql
--
-- 作用：
-- 为当前认证域内需要维护 updated_at 的表提供统一触发器函数。
--
-- 为什么单独拆成一个脚本：
-- 1. 这是跨表复用的基础对象，不属于某一张具体业务表；
-- 2. 后续新增 friendships、conversations、messages 等表时，也可以直接复用；
-- 3. 把“可复用基础对象”和“具体表定义”分开，迁移结构会更清晰。
--
-- 使用方式：
-- - 各表只需要定义 updated_at 字段；
-- - 再创建 BEFORE UPDATE 触发器，执行 set_updated_at() 即可；
-- - 这样业务代码不需要每次 UPDATE 时手工补 updated_at = NOW()。

-- set_updated_at():
-- 触发器在每次 UPDATE 前执行，把即将写回的新行的 updated_at 刷成当前时间。
CREATE OR REPLACE FUNCTION set_updated_at()
RETURNS TRIGGER
AS $$
BEGIN
    -- NEW 代表“更新后的那一行”。
    -- 这里直接覆盖 NEW.updated_at，保证任意业务更新都会刷新更新时间。
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- 元数据注释：
-- 方便后续用 \df+ 或数据库工具查看函数职责，不需要反查迁移脚本。
COMMENT ON FUNCTION set_updated_at() IS
    '统一维护 updated_at 的触发器函数。';
